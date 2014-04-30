module.exports = {
	readTimestamp: function(buf) {
		var ms = buf.readUInt32BE(0) * 0x100000000 + buf.readUInt32BE(4);
		if (ms == 0)
			return undefined;

		return new Date(ms);
	},
	writeTimestamp: function(buf, ts, offset) {
		offset = offset || 0;
		if (!ts) {
			buf.writeUInt32BE(0, offset);
			buf.writeUInt32BE(0, offset + 4);
			return;
		}

		var ms = (ts instanceof Date ? ts.getTime() : ts);

		buf.writeUInt32BE(Math.floor(ms / 0x100000000), offset);
		buf.writeInt32BE(ms & 0xffffffff, offset + 4);
	}
};
