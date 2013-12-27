var http = require('http');

var prev = [];
var interval = 1000;
var burst = 10;

function compute_until(now) {
    if (prev.length >= burst) {
        var a = prev[prev.length - burst] + burst * interval;
        var b = Math.max(now, prev[prev.length - 1]) + interval;
        return b;
    }
}

var until, until_server;

function rate_limit(cb, failcb) {
    var now = (new Date()).getTime();

    console.log("Got a request at", now);
    if (prev.length > burst - 1) {
        prev.splice(0, prev.length - burst + 1);
    }
    prev = prev.filter(function (tm) {
        return tm >= now - (burst * interval);
    });
    if (prev.length > burst) {
        prev.splice(0, prev.length - burst);
    }

    if (until && until > now + 5) {
        if (until > now + 5000) {
            console.log("Rejecting (would be", "" + (until - now) + "ms delay)");
            setImmediate(function() { failcb(Math.floor((until - now) / 1000)) });
            return;
        } else {
            console.log("Will allow it through at", until, "(" + (until - now) + "ms delay)");
            setTimeout(cb, until - now);
        }
    } else {
        console.log("Allowing it through immediately");
        setImmediate(cb);
    }

//    prev.push(until_server > now ? until_server : now); // harsh
    prev.push(until > now ? until : now); // permissive

    console.log(prev);

    until_server = compute_until(now);
    setTimeout(function() { until = until_server }, 2000);
    console.log("Next is allowed at", until, "(" + (until - now) + "ms from now)");
    console.log("");
}

var server = http.createServer();

server.on('request', function (req, res) {
    rate_limit(function() {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.write("OK\n");
        res.end();
    },
    function(seconds) {
        res.writeHead(503, { "Content-Type": "text/plain", "Retry-After": seconds });
        res.write("503\n");
        res.end();
    });
});

server.listen(8000);
