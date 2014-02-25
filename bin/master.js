#!/usr/bin/env node

var Smockron = require('../lib/smockron'),
    config = require('config');

var master = new Smockron.Master(config);

master.listen();
