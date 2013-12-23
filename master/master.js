var zmq = require('zmq'),
    redis = require('then-redis'),
    q = require('q');

var accountingSocket = zmq.socket('sub');
accountingSocket.bindSync('tcp://0.0.0.0:10004');
accountingSocket.subscribe('');

var controlSocket = zmq.socket('pub');
controlSocket.bindSync('tcp://0.0.0.0:10005');

accountingSocket.on('message', function(domain, status, identifier, rcvTS, delayTS, loginfo) {
  console.log(domain.toString(), status.toString(), identifier.toString(), rcvTS.toString());
  controlSocket.send([domain, 'DELAY_UNTIL', identifier, '12345']);
});
