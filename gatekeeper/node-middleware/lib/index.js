var zmq = require('zmq');

module.exports = Smockron;

function Smockron(opts) {
  this.domain = opts.domain;
  this.accounting = opts.accounting;
  this.control = opts.control;
  this.identifierCB = opts.identifierCB;

  this._connect();
};

Smockron.REMOTE_ADDR = function(req) {
  return req.ip;
};

Smockron.prototype._connect = function() {
  this.accountingSocket = zmq.socket('pub');
  this.accountingSocket.connect(this.accounting);

  this.controlSocket = zmq.socket('sub');
  this.controlSocket.connect(this.control);
  this.controlSocket.subscribe(this.domain);
  this.controlSocket.on('message', this._onControl.bind(this));
};

Smockron.prototype._sendAccounting = function(status, identifier, rcvTS, delayTS, logInfo) {
  this.accountingSocket.send([
    this.domain, status, identifier, rcvTS, delayTS, logInfo
  ]);
};

Smockron.prototype._onControl = function(domain, command, identifier, arg1) {
  if (!command) {
    console.warn("Control message with no command");
    return;
  }
  command = command.toString();
  if (command == "DELAY_UNTIL") {
    this._delayUntil(domain.toString(), identifier.toString(), parseFloat(arg1.toString()));
  } else {
    console.warn("Control message with unknown command ", command);
    return;
  }
};

Smockron.prototype._delayUntil = function(domain, identifier, ts) {
  console.log("Delay", identifier, "until", ts, "for", domain);
};

Smockron.prototype.middleware = function() {
  var self = this;

  return function (req, res, next) {
    var now = (new Date()).getTime();

    var identifier = self.identifierCB(req);
    self._sendAccounting('ACCEPTED', identifier, now, "", "");
    setImmediate(next);
  };
};
