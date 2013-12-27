var express = require('express'),
    Smockron = require('./lib/');

var smockron = new Smockron({
    domain: "default",
    server: "localhost",
    identifierCB: Smockron.REMOTE_ADDR
});

var app = express();

app.use(express.logger());

app.get('/', smockron.middleware(), function(req, res) {
  res.send("OK\n");
});

app.listen(8000);
