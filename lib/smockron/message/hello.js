function Hello(opts) {
	for (var attr in opts) this[attr] = opts[attr];
}

Hello.parse = function(msg) {
	var flags = msg.data[0].readUInt16BE(0);

	return new Hello({
		syncReq: flags & 0x0001 ? true : false,
		domains: msg.data[1].toString('utf8').split("\0")
	});
};

Hello.prototype.format = function() {
	var flags = new Buffer(2);
	flags.writeUInt16LE(this.syncReq ? 0x0001 : 0, 0);

	return {
		type: 0x0001,
		data: [
			flags,
			new Buffer(this.domains.join("\0"), 'utf8')
		]
	};
};

module.exports = Hello;
