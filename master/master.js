var Smockron = require('./lib');

var master = new Smockron.Master({
    listen: '0.0.0.0',
    dataStore: 'localhost',
    domains: {
        "default": {
            interval: 1000,
            burst: 10
        }
    }
});

master.listen();
