var zmq = require('zmq'),
    util = require('util'),
    events = require('events');


module.exports = Smockron;

Smockron.Client = function(opts) {
  events.EventEmitter.call(this);

  this._domain = opts.domain;
  this.server = this._parseConnectionString(opts.server);
  this.socket = {};
};

util.inherits(Smockron.Client, events.EventEmitter);

Smockron.Client.prototype._parseConnectionString = function(connectionString) {
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

Smockron.Client.prototype.connect = function() {
  this.socket.accounting = zmq.socket('pub');
  this.socket.accounting.connect(this.server.accounting);

  this.socket.control = zmq.socket('sub');
  this.socket.control.connect(this.server.control);
  this.socket.control.subscribe(this._domain + "\0");
  this.socket.control.on('message', this._onControl.bind(this));
};

Smockron.Client.prototype._onControl = function() {
  try {
    controlMsg = this._parseControl(arguments);
    this.emit('control', controlMsg);
  } catch (e) {
    console.warn("Error decoding control message", e);
  }
};

Smockron.Client.prototype._parseControl = function(data) {
  var msg = {};
  if (data.length < 3) {
    throw("Too-short control message");
  }

  var decoded = Array.prototype.slice.call(data, 0).map(function (buf) { return buf.toString() });

  var ret = {
    domain: decoded[0].replace(/\0$/, ''),
    command: decoded[1],
    identifier: decoded[2],
    args: decoded.slice(3)
  };
  if (ret.command == 'DELAY_UNTIL') {
    ret.ts = parseFloat(ret.args[0]);
  }
  return ret;
};

Smockron.Client.prototype.sendAccounting = function(opts) {
  var frames = [
    this._domain + "\0",
    opts.status,
    opts.identifier,
    opts.rcvTS,
    "",
    ""
  ];
  if (opts.delayTS !== undefined)
    frames[4] = opts.delayTS;
  if (opts.logInfo !== undefined)
    frames[5] = opts.logInfo;
  this.socket.accounting.send(frames);
};

/* END CLIENT */

function Smockron(opts) {
  this.identifierCB = opts.identifierCB;

  this.client = new Smockron.Client({
    domain: opts.domain,
    server: opts.server
  });

  this.delayed = {};
  this.queue = {};
  this.timer = {};

  this.client.on('control', this._onControl.bind(this));
  this.client.connect();

  setInterval(this._cleanup.bind(this), 1000).unref();
};

Smockron.prototype._cleanup = function() {
  var now = (new Date()).getTime();

  for (var key in this.delayed) {
    if (this.delayed[key] < now)
      delete this.delayed[key];
  }
};

Smockron.REMOTE_ADDR = function(req) {
  return req.ip;
};

Smockron.prototype._onControl = function(msg) {
  if (msg.command == 'DELAY_UNTIL') {
    this._delayUntil(msg);
  } else {
    console.warn("Control message with unknown command ", msg.command);
    return;
  }
};

Smockron.prototype._delayUntil = function(msg) {
  if (this.delayed[msg.identifier] === undefined || msg.ts > this.delayed[msg.identifier])
    this.delayed[msg.identifier] = msg.ts;
};

Smockron.prototype.enqueue = function(identifier, cb) {
  if (!this.queue[identifier])
    this.queue[identifier] = [];
  if (this.queue[identifier].length >= 1) {
    return false;
  } else {
    this.queue[identifier].push(cb);
    this.ensureTimer(identifier);
    return true;
  }
};

Smockron.prototype.ensureTimer = function(identifier) {
  if (this.timer[identifier])
    clearTimeout(this.timer[identifier]);
  var wait;
  if (this.delayed[identifier]) {
    var now = (new Date()).getTime();
    wait = this.delayed[identifier] - now;
  } else {
    wait = 10;
  }

  this.timer[identifier] = setTimeout(
      function () { this.dequeue(identifier) }.bind(this),
      wait
  );
};

Smockron.prototype.dequeue = function(identifier) {
  if (this.queue[identifier]) {
   if (this.queue[identifier].length) {
     var cb = this.queue[identifier].pop();
     setImmediate(cb);
   }
   if (this.queue[identifier].length) {
     this.ensureTimer(identifier);
   } else {
     delete this.queue[identifier];
     delete this.timer[identifier];
   }
  }
};

Smockron.prototype.middleware = function(rejectCB) {
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
      if (delayTS > now + 5000 || !self.enqueue(identifier, next)) {
        setTimeout(function () { rejectCB(req, res) }, 1);
        accounting.status = 'REJECTED';
      } else {
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
