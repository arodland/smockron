var ts = require('./ts');
var DelayUntil = require('./delayuntil');

function HelloReply(opts) {
	for (var attr in opts) this[attr] = opts[attr];
}

HelloReply.parse = function(msg) {
	var flags = msg.data[0].readUInt16BE(0);

	var delays = [];
	for (var i = 0 ; i < msg.data.length ; i += 3) {
		delays.push(new DelayUntil({
			domain: msg.data[i+0].toString('utf8'),
			identifier: msg.data[i+1].toString('utf8'),
			delayTS: this.readTimestamp(msg.data[i+2])
		}));
	}

	return new HelloReply({
		more: flags & 0x0001 ? true : false,
		delays: delays
	});
};

HelloReply.prototype.format = function() {
	var flags = new Buffer(2);
	flags.writeUInt16BE(this.more ? 0x0001 : 0, 0);

	var data = [ flags ];

	if (this.delays) {
		for (var i = 0 ; i < this.delays.length ; i++) {
			var delayOut = this.delays[i].format();
			data = data.concat(delayOut.data);
		}
	}

	return {
		type: 0x8101,
		data: data
	};
};

module.exports = HelloReply;
