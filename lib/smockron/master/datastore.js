var redis = require('then-redis'),
    when = require('when');

function DataStore(opts) {
  this.host = opts.host;
  this.redis = redis.createClient(this.host);
};

DataStore.prototype._getKey = function(opts) {
  return 'throttle;' + opts.domain + ';' + opts.identifier;
};

var _luaScript = [
  "local key, now, interval, burst = KEYS[1], ARGV[1], ARGV[2], ARGV[3]",
  "local prev = redis.call('get', key)",
  "local new",
  "if prev and tonumber(prev) >= now - burst then",
  "  new = prev + interval",
  "else",
  "  new = now - burst + interval",
  "end",
  "  redis.call('set', key, new)",
  "  redis.call('pexpireat', key, now + burst)",
  "  return new"
].join("\n");

DataStore.prototype.loadLuaScript = function() {
  var self = this;
  return self.redis.script('load', _luaScript).then(function (sha) {
    self.luaScriptSHA = sha;
    return sha;
  });
};

DataStore.prototype.execScript = function(sha, opts) {
  var key = this._getKey(opts);

  return this.redis.evalsha(
      sha,
      1, key,
      Math.max(opts.ts, opts.now), opts.interval, opts.burst
  );
};

DataStore.prototype.logAccess = function(opts) {
  var self = this;
  var getSha = self.luaScriptSHA ? self.luaScriptSHA : self.loadLuaScript();
  return when(getSha).then(function (sha) {
    return self.execScript(sha, opts).catch(function (err) {
      if (err.message.match(/NOSCRIPT/)) {
        delete self.luaScriptSHA;
        return self.logAccess(opts); // Try again
      } else {
        return when.reject(err);
      }
    })
  }).catch(console.warn);
};

DataStore.prototype.getNext = function(opts) {
  var key = this._getKey(opts);
  return this.redis.get(key);
};

module.exports = DataStore;
