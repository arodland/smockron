var ts = require('./ts');

function Accounting(opts) {
	for (var attr in opts) this[attr] = opts[attr];
}

var statusFromCode = [
	'ACCEPTED',
	'DELAYED',
	'REJECTED'
];

var statusToCode = {
	'ACCEPTED': 0,
	'DELAYED': 1,
	'REJECTED': 2
};

Accounting.parse = function(msg) {
	var statusCode = msg.data[2].readUInt8(0);
	var status = statusFromCode[statusCode];
   	if (!status)
		throw new Error("invalid status");

	return new Accounting({
		domain: msg.data[0].toString('utf8'),
		identifier: msg.data[1].toString('utf8'),
		status: status,
		rcvTS: ts.readTimestamp(msg.data[3]),
		delayTS: ts.readTimestamp(msg.data[4]),
		logInfo: msg.data[5].toString('utf8')
	});
};

Accounting.prototype.format = function() {
	var status = new Buffer(1);
	status.writeUInt8(statusToCode[this.status], 0);
	var rcvTS = new Buffer(8);
	ts.writeTimestamp(rcvTS, this.rcvTS);
	var delayTS = new Buffer(8);
	ts.writeTimestamp(delayTS, this.delayTS);

	return {
		type: 0x0101,
		data: [
			new Buffer(this.domain, 'utf8'),
			new Buffer(this.identifier, 'utf8'),
			status,
			rcvTS,
			delayTS,
			new Buffer(this.logInfo || '', 'utf8')
		]
	};
};

module.exports = Accounting;
