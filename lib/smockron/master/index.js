var Server = require('./server'),
    DataStore = require('./datastore'),
    Stats = require('./stats');

function Master(config) {
  this.config = config;
  this.server = new Server(this.config.server);
  this.dataStore = new DataStore(this.config.datastore);
  if (this.config.stats) {
    this.stats = new Stats(this.config.stats);
  }
  this.domains = this.configureDomains(this.config.domains);
  this.server.on('accounting', this._onAccounting.bind(this));
};

Master.prototype.parseInterval = function(interval) {
  if (typeof(interval) == 'number')
    return interval;

  var suffixes = {
    ""        :        1,
    "ms"      :        1,
    "msec"    :        1,
    "s"       :     1000,
    "sec"     :     1000,
    "second"  :     1000,
    "seconds" :     1000,
    "m"       :    60000,
    "min"     :    60000,
    "minute"  :    60000,
    "minutes" :    60000,
    "h"       :  3600000,
    "hr"      :  3600000,
    "hour"    :  3600000,
    "hours"   :  3600000,
    "d"       : 86400000,
    "day"     : 86400000,
    "days"    : 86400000
  };
  var re = new RegExp('^\\s*(\\d+)?\\s*(' + Object.keys(suffixes).join('|') + ')\\s*$');
  var m = interval.match(re);
  if (m) {
    var base = m[1] ? parseInt(m[1], 10) : 1;
    var mult = suffixes[m[2]];
    return base * mult;
  } else {
    throw "bad interval";
  }
};

Master.prototype.parseRate = function(rate) {
  var m = rate.match(/^\s*(\d+)\s*(per|\/)\s*(.*)$/);
  if (!m)
    throw "bad rate";
  var num = parseInt(m[1], 10);
  var interval = this.parseInterval(m[3]);
  return Math.round(interval / num);
};

Master.prototype.configureDomains = function(domains) {
  var ret = {};
  for (var name in domains) {
    ret[name] = {};
    if (domains[name].rate)
      ret[name].interval = this.parseRate(domains[name].rate);
    else if (domains[name].interval)
      ret[name].interval = this.parseInterval(domains[name].interval);
    if (typeof(domains[name].burst) == 'number')
      ret[name].burst = ret[name].interval * domains[name].burst;
    else if (domains[name].burst.match(/^\d+$/))
      ret[name].burst = ret[name].interval * parseInt(domains[name].burst, 10);
    else
      ret[name].burst = this.parseInterval(domains[name].burst);
  }
  return ret;
};

Master.prototype.listen = function() {
  this.server.listen();
};

Master.prototype._onAccounting = function(msg) {
  var ts;
  var domain = this.domains[msg.domain];
  var now = (new Date()).getTime();

  if (!domain) {
    console.warn("Received accounting message for unknown domain", msg.domain);
    return;
  }

  if (msg.status == 'REJECTED') {
    return; // Do nothing, for now.
  } else if (msg.status == 'ACCEPTED') {
    ts = msg.rcvTS;
  } else if (msg.status == 'DELAYED') {
    ts = msg.delayTS;
  }

  var self = this;

  self.dataStore.logAccess({
    domain: msg.domain,
    identifier: msg.identifier,
    ts: ts,
    interval: domain.interval,
    burst: domain.burst,
    now: now
  }).then(function (delayUntil) {
    if (delayUntil > now) {
      self.server.sendControl({
        domain: msg.domain,
        identifier: msg.identifier,
        command: 'DELAY_UNTIL',
        args: [ delayUntil ]
      });
    }
  });

  if (self.stats) {
    self.stats.logAccess(msg);
  }

};

Master.prototype.shouldDelay = function(domainName, identifier, domain, now) {
  return this.dataStore.getNext({
    domain: domainName,
    identifier: identifier,
    burst: domain.burst
  }).then(function (next) {
    if (next === undefined || next === null || next <= now) {
      return when.reject();
    } else {
      return next;
    }
  }, function(e) {
    return when.reject(e);
  }
  );
}

module.exports = Master;
