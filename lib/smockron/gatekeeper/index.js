var Client = require('./client');

function Gatekeeper(opts) {
  this.identifierCB = opts.identifierCB;

  this.client = new Client({
    domain: opts.domain,
    server: opts.server
  });

  this.delayed = {};

  this.client.on('control', this._onControl.bind(this));
  this.client.connect();

  setInterval(this._cleanup.bind(this), 1000).unref();
};

Gatekeeper.prototype._cleanup = function() {
  var now = (new Date()).getTime();

  for (var key in this.delayed) {
    if (this.delayed[key] < now)
      delete this.delayed[key];
  }
};

Gatekeeper.REMOTE_ADDR = function(req) {
  return req.ip;
};

Gatekeeper.prototype._onControl = function(msg) {
  if (msg.command == 'DELAY_UNTIL') {
    this._delayUntil(msg);
  } else {
    console.warn("Control message with unknown command ", msg.command);
    return;
  }
};

Gatekeeper.prototype._delayUntil = function(msg) {
  if (this.delayed[msg.identifier] === undefined || msg.ts > this.delayed[msg.identifier])
    this.delayed[msg.identifier] = msg.ts;
};

Gatekeeper.prototype.middleware = function(rejectCB) {
  var self = this;
  if (rejectCB === undefined) {
    rejectCB = function(req, res) {
      res.send(503, "Rejected\n");
    };
  }

  return function (req, res, next) {
    var now = (new Date()).getTime();
    var identifier = self.identifierCB(req);
    var delayTS = self.delayed[identifier];
    var accounting = {
      identifier: identifier,
      rcvTS: now
    };

    if (delayTS && delayTS > now) {
      if (delayTS > now + 5000) {
        setImmediate(function () { rejectCB(req, res) });
        accounting.status = 'REJECTED';
      } else {
        setTimeout(next, delayTS - now);
        accounting.status = 'DELAYED';
        accounting.delayTS = delayTS;
      }
    } else {
      accounting.status = 'ACCEPTED';
      setImmediate(next);
    }

    self.client.sendAccounting(accounting);
  };
};

module.exports = Gatekeeper;
