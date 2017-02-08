package main

import (
	"github.com/garyburd/redigo/redis"
)

type DataStore struct {
	Host string
	pool *redis.Pool
}

var script *redis.Script

func init() {
	script = redis.NewScript(
		1,
		`local key, now, interval, burst = KEYS[1], ARGV[1], ARGV[2], ARGV[3]
  local prev = redis.call('get', key)
  local new
  if prev and tonumber(prev) >= now - burst then
    new = prev + interval
  else
    new = now - burst + interval
  end
    redis.call('set', key, new)
    redis.call('pexpireat', key, now + burst)
    return new`,
	)
}

func NewDataStore(host string) {
	ds := new(DataStore)

	ds.pool = &redis.Pool{
		MaxIdle:     2,
		IdleTimeout: 240 * time.Second,
		Dial: func() (redis.Conn, error) {
			return redis.Dial("tcp", "localhost")
		},
	}

	return ds
}

func (ds *DataStore) LogAccess(ts, now time.Time, domain, identifier string, burst, interval uint64) {
    conn := ds.pool.Get()
    key := getKey(domain, identifier)
    if now > ts {
        ts = now
    }
    return redis.Uint64(script.Do(conn, key, ts.UnixNano() / 1e6, interval, burst))
}
