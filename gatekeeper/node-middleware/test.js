var express = require('express'),
    Smockron = require('./lib/');

var smockron = new Smockron({
    domain: "testdomain",
    accounting: "tcp://localhost:10004",
    control: "tcp://localhost:10005",
    identifierCB: Smockron.REMOTE_ADDR
});

var app = express();

app.get('/', smockron.middleware(), function(req, res) {
  res.send('OK');
});

app.listen(8000);
