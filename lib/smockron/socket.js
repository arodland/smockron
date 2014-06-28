var events = require('events'),
	dgram = require('dgram'),
	util = require('util'),
	message = require('./message');

function Socket(opts) {
	events.EventEmitter.call(this);
	this.port = opts.port || 10004;
	this.proto = opts.proto || 'udp4';

	this.udp = dgram.createSocket(this.proto);
	this.udp.bind(this.port);

	this.udp.on('message', this.onPacket.bind(this));
}

util.inherits(Socket, events.EventEmitter);

Socket.prototype.parsePacket = function(msg) {
	if (msg.length < 4)
		throw new Error("too-short message received");
	var type = msg.readUInt16BE(0);
	var off = 2, data = [], foundTrailer = false;
	while (off < msg.length) {
		var segmentLength = msg.readUInt16BE(off);
		if (segmentLength == 0xffff) {
			foundTrailer = true;
			break;
		}
		off += 2;

		if (off + segmentLength > msg.length)
			throw new Error("segment runs off the end of message");
		data.push(msg.slice(off, off + segmentLength));
		off += segmentLength;
	}
	if (!foundTrailer)
		throw new Error("message without trailer, probably truncated");

	return {
		type: type,
		data: data
	};
}

Socket.prototype.onPacket = function(packet, rinfo) {
	var msg = this.parsePacket(packet);
	this.dispatchMessage(msg, rinfo);
};

Socket.prototype.dispatchMessage = function(msg, rinfo) {
	if (msg.type == 0x0001) {
		this.emit('hello', message.Hello.parse(msg), rinfo);
	} else if (msg.type == 0x0101) {
		this.emit('accounting', message.Accounting.parse(msg), rinfo);
	} else if (msg.type == 0x8001) {
		this.emit('delayUntil', message.DelayUntil.parse(msg), rinfo);
	} else if (msg.type == 0x8101) {
		this.emit('sync', message.Sync.parse(msg), rinfo);
	} else {
		this.emit('unknownMessage', msg, rinfo);
	}
};

Socket.prototype.makePacket = function(msg) {
	var formatted = msg.format();
	var buffers = [];

	var type = new Buffer(2);
	type.writeUInt16BE(formatted.type, 0);
	buffers.push(type);

	for (var i = 0 ; i < formatted.data.length ; i++) {
		var len = formatted.data[i].length;
		var lenBuf = new Buffer(2);
		lenBuf.writeUInt16BE(len, 0);
		buffers.push(lenBuf);
		buffers.push(formatted.data[i]);
	}
	var trailer = new Buffer(2);
	trailer.writeUInt16BE(0xffff, 0);
	buffers.push(trailer);

	return Buffer.concat(buffers);
};

Socket.prototype.send = function(msg, dest) {
	var packet = this.makePacket(msg);
	this.udp.send(packet, 0, packet.length, dest.port, dest.host);
};

module.exports = Socket;
