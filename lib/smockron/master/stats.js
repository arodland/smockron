var statsd = require('node-statsd').StatsD;

function Stats(opts) {
  this.statsd = new statsd(opts);
};

Stats.prototype.logAccess = function(msg) {
  this.statsd.increment('request.status.' + msg.status);
  this.statsd.increment('request.total');
  if (msg.status == 'DELAYED') {
    var delayedBy = msg.delayTS - msg.rcvTS;
    this.statsd.timing('request.delayed_by', delayedBy);
  }
};

module.exports = Stats;
