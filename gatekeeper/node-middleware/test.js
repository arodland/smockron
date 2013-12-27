var express = require('express'),
    Smockron = require('./lib/');

var smockron = new Smockron({
    domain: "testdomain",
    server: "localhost",
    identifierCB: Smockron.REMOTE_ADDR
});

var app = express();

app.get('/', smockron.middleware(), function(req, res) {
  res.send('OK');
});

app.listen(8000);
