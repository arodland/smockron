var zmq = require('zmq'),
    util = require('util'),
    events = require('events');

function Server(opts) {
  events.EventEmitter.call(this);

  this.listenAddr = this._parseConnectionString(opts.listen);
  this.socket = {};
};

util.inherits(Server, events.EventEmitter);

Server.prototype._parseConnectionString = function(connectionString) {
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

Server.prototype.listen = function() {
  this.socket.accounting = zmq.socket('sub');
  this.socket.accounting.bindSync(this.listenAddr.accounting);
  this.socket.accounting.subscribe(''); // Receive messages for all domains

  this.socket.control = zmq.socket('pub');
  this.socket.control.bindSync(this.listenAddr.control);

  this.socket.accounting.on('message', this._onAccounting.bind(this));
};

Server.prototype._onAccounting = function() {
  try {
    accountingMsg = this._parseAccounting(arguments);
    if (accountingMsg.type == 'ACCOUNTING') {
      this.emit('accounting', accountingMsg);
    } else if (accountingMsg.type == 'RESYNC') {
      this.emit('resync', accountingMsg);
    }
  } catch (e) {
    console.warn("Accounting error", e);
  }
};

Server.prototype._parseAccounting = function(data) {
  var msg = {};
  var decoded = Array.prototype.slice.call(data, 0).map(function (buf) { return buf.toString() });
  if (data.length < 2) {
    throw("Too-short message on accounting socket");
  }
  var domain = decoded[0].replace(/\0$/, '');

  if (decoded[1] == 'RESYNC') {
    return {
      type: 'RESYNC',
      domain: domain
    };
  } else if (decoded[1] == 'ACCEPTED' || decoded[1] == 'DELAYED' || decoded[1] == 'REJECTED') {
    if (data.length < 5) {
      throw("Too-short accounting message");
    }

    var ret = {
      type: 'ACCOUNTING',
      domain: domain,
      status: decoded[1],
      identifier: decoded[2],
      rcvTS: parseFloat(decoded[3])
    };


    if (decoded[4])
      ret.delayTS = parseFloat(decoded[4]);
    if (decoded[5])
      ret.logInfo = parseFloat(decoded[5]);

    return ret;
  } else {
    throw("Unknown message of type " + decoded[1] + " on accounting socket");
  }
};

Server.prototype.sendControl = function(opts) {
  var frames = [
    opts.domain + "\0",
    opts.command
  ];
  if (opts.identifier) {
    frames.push(opts.identifier);
  }
  if (opts.args) {
    frames = frames.concat(opts.args);
  }
  this.socket.control.send(frames);
};

module.exports = Server;
