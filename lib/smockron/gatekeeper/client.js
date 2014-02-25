var zmq = require('zmq'),
    util = require('util'),
    events = require('events');

function Client(opts) {
  events.EventEmitter.call(this);

  this._domain = opts.domain;
  this.server = this._parseConnectionString(opts.server);
  this.socket = {};
};

util.inherits(Client, events.EventEmitter);

Client.prototype._parseConnectionString = function(connectionString) {
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

Client.prototype.connect = function() {
  this.socket.accounting = zmq.socket('pub');
  this.socket.accounting.connect(this.server.accounting);

  this.socket.control = zmq.socket('sub');
  this.socket.control.connect(this.server.control);
  this.socket.control.subscribe(this._domain + "\0");
  this.socket.control.on('message', this._onControl.bind(this));
};

Client.prototype._onControl = function() {
  try {
    controlMsg = this._parseControl(arguments);
    this.emit('control', controlMsg);
  } catch (e) {
    console.warn("Error decoding control message", e);
  }
};

Client.prototype._parseControl = function(data) {
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

Client.prototype.sendAccounting = function(opts) {
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

module.exports = Client;
