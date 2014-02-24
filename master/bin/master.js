var Smockron = require('../lib'),
    config = require('config');

var master = new Smockron.Master(config);

master.listen();
