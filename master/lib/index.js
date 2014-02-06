var zmq = require('zmq'),
    redis = require('then-redis'),
    when = require('when'),
    util = require('util'),
    events = require('events');

module.exports = Smockron;

Smockron.Server = function(opts) {
  events.EventEmitter.call(this);

  this.listenAddr = this._parseConnectionString(opts.listen);
  this.socket = {};
};

util.inherits(Smockron.Server, events.EventEmitter);

Smockron.Server.prototype._parseConnectionString = function(connectionString) {
  var m;
  if (connectionString !== undefined
      && (m = connectionString.match(/^(?:(\w+):\/\/)?(.*?)(?::(\d+))?$/))) {
        var scheme = m[1],
            host = m[2],
            port = m[3];
        if (scheme === undefined)
          scheme = 'tcp';
        if (host === undefined)
          throw "Host is required";
        if (port === undefined)
          port = 10004;
        else
          port = parseInt(port, 10);
        return {
          accounting: scheme + '://' + host + ':' + port,
            control: scheme + '://' + host + ':' + (port + 1)
        };
      } else {
        throw "Invalid connection string '" + connectionString + "'";
      }
};

Smockron.Server.prototype.listen = function() {
  this.socket.accounting = zmq.socket('sub');
  this.socket.accounting.bindSync(this.listenAddr.accounting);
  this.socket.accounting.subscribe(''); // Receive messages for all domains

  this.socket.control = zmq.socket('pub');
  this.socket.control.bindSync(this.listenAddr.control);

  this.socket.accounting.on('message', this._onAccounting.bind(this));
};

Smockron.Server.prototype._onAccounting = function() {
  try {
    accountingMsg = this._parseAccounting(arguments);
    this.emit('accounting', accountingMsg);
  } catch (e) {
    console.warn("Accounting error", e);
  }
};

Smockron.Server.prototype._parseAccounting = function(data) {
  var msg = {};
  if (data.length < 5) {
    throw("Too-short accounting message");
  }

  var decoded = Array.prototype.slice.call(data, 0).map(function (buf) { return buf.toString() });

  var ret = {
    domain: decoded[0].replace(/\0$/, ''),
    status: decoded[1],
    identifier: decoded[2],
    rcvTS: parseFloat(decoded[3])
  };

  if (decoded[4])
    ret.delayTS = parseFloat(decoded[4]);
  if (decoded[5])
    ret.logInfo = parseFloat(decoded[5]);

  return ret;
};

Smockron.Server.prototype.sendControl = function(opts) {
  var frames = [
    opts.domain + "\0",
    opts.command,
    opts.identifier
  ].concat(
    opts.args
  );
  this.socket.control.send(frames);
};

/* END SERVER */

Smockron.DataStore = function(opts) {
  this.server = opts.server;
  this.redis = redis.createClient(this.server);
};

Smockron.DataStore.prototype._getKey = function(opts) {
  return 'throttle;' + opts.domain + ';' + opts.identifier;
};

Smockron.DataStore.prototype.logAccess = function(opts) {
  var key = this._getKey(opts);
  // TODO: WATCH key, do the SET in a MULTI, and retry if the MULTI was aborted by
  // someone else modifying key
  var self = this;
  self.redis.get(key).then(function (val) {
    var next;
    if (val === undefined || val === null || val < opts.now - opts.burst * opts.interval) {
      next = opts.now - opts.burst * opts.interval;
    } else {
      next = parseInt(val, 10) + opts.interval;
    }
    self.redis.set(key, next);
    self.redis.pexpireat(key, opts.now + opts.burst * opts.interval);
  });
};

Smockron.DataStore.prototype.getNext = function(opts) {
  var key = this._getKey(opts);
  return this.redis.get(key);
};

/* END DATASTORE */

Smockron.Master = function(opts) {
  this.server = new Smockron.Server({
    listen: opts.listen
  });

  this.dataStore = new Smockron.DataStore({
    server: opts.dataStore
  });

  this.domains = opts.domains;

  this.server.on('accounting', this._onAccounting.bind(this));
};

Smockron.Master.prototype.listen = function() {
  this.server.listen();
};

Smockron.Master.prototype._onAccounting = function(msg) {
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

  this.dataStore.logAccess({
    domain: msg.domain,
    identifier: msg.identifier,
    ts: ts,
    interval: domain.interval,
    burst: domain.burst,
    now: now
  });

  var self = this;
  this.shouldDelay(msg.domain, msg.identifier, domain, now).then(function (delayUntil) {
    self.server.sendControl({
      domain: msg.domain,
      identifier: msg.identifier,
      command: 'DELAY_UNTIL',
      args: [ delayUntil ]
    });
  }, function (e) { if (e) console.warn(e) });
};

Smockron.Master.prototype.shouldDelay = function(domainName, identifier, domain, now) {
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

function Smockron() {
  /* ... */
}

