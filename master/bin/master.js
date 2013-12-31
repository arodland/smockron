var Smockron = require('../lib'),
    config = require('config');

var master = new Smockron.Master({
    listen: config.server.listen,
    dataStore: config.datastore.host,
    domains: config.domains
});

master.listen();
