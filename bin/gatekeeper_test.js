var express = require('express'),
    Smockron = require('../lib/smockron');

var gk = new Smockron.Gatekeeper({
    domain: "default",
    server: "localhost",
    identifierCB: Smockron.Gatekeeper.REMOTE_ADDR
});

var app = express();

app.use(express.logger());

app.get('/', gk.middleware(), function(req, res) {
  res.send("OK\n");
});

app.listen(8000);
