var ts = require('./ts');

function DelayUntil(opts) {
	for (var attr in opts) this[attr] = opts[attr];
}

DelayUntil.parse = function(msg) {
	return new DelayUntil({
		domain: msg.data[0].toString('utf8'),
		identifier: msg.data[1].toString('utf8'),
		delayTS: this.readTimestamp(msg.data[2])
	});
};

DelayUntil.prototype.format = function() {
	var delayTS = new Buffer(8);
	ts.writeTimestamp(delayTS, this.delayTS);
	return {
		type: 0x8001,
		data: [
			new Buffer(this.domain, 'utf8'),
			new Buffer(this.idneitifer, 'utf8'),
			delayTS
		]
	};
};

module.exports = DelayUntil;
