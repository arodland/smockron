## Synopsis:

Smockron is a system for rate-limiting access to resources in a distributed
system -- for example, limiting requests to a REST service which runs many
instances on many machines. Its goal is transparency through efficiency and
reliability -- well-behaved clients should never know it's there because it
has the smallest possible impact on the performance and availaiblity of the
services it protects.

Smockron consists of a "master" server (running on Node.js with a redis data
store) and distributed "gatekeepers", which are embedded into applications
or proxies. Currently the only available gatekeeper is a Node Connect /
Express middleware, with a gatekeeper nginx module in the works.

## Definitions:

**Resource:** Something you want to rate-limit (e.g. requests to an HTTP
application, or a single route or entity within that application).

**Domain:** An identifier for a resource or group of multiple resources.
Rate-limiting is performed on a per-domain basis.

**Client:** An entity that sends requests to a resource.

**Client Identifier:** Something that identifies a client (e.g. IP address
or username).

**Gatekeeper:** A piece of software that controls access to a resource; it
can delay or reject requests to ensure that a client does not send too many
requests to the resource (rate limiting).

**Master:** A piece of software that monitors traffic through the
gatekeepers, applies rate limiting logic, and instructs the gatekeepers as
to when and whether requests are allowed.

**Rule:** a formula involving a rate (requests per time period) and a burst
(a number of requests which can be made before the rate takes effect). A
domain can have zero or more rules; it will throttle a request if any rule
says it should be throttled.

## Rate-Limiting logic:

If the number of requests in the past `(burst / rate)` is less than `burst`,
then another request is acceptable immediately. Otherwise let `start` be the
greater of the current time and the last timestamp in the history; the next
request will be allowed at the time `start + (1 / rate)`. The timestamps
stored in the history should be the received timestamp for accepted
requests, and the delayed timestamp for delayed requests; this ensures that
the policy can be enforced with a history of no more than `burst`
timestamps.

## Protocol: 

Gatekeepers and masters communicate using a pair of ZeroMQ PUB/SUB sockets.
Both are physically outgoing from the gatekeeper (bind() on the master,
connect() on the gatekeeper), but one (the accounting socket) sends messages
from gatekeeper to master, while the other (the control socket) sends
messages from master to gatekeepers. Both sockets use the domain as the
pubsub topic. A ZeroMQ router may be employed to shard accounting messages
to multiple masters, provided that all messages for the same domain go to
the same master.

When the master receives an accounting message, it applies the "Throttling
logic" above to determine when the next request for the given identifier is
allowed on the given domain, using the rules for that domain. If the next
request is allowable right now it does nothing (except logging and
statistics collection); otherwise it publishes a control message informing
gatekeepers of the next allowable request timestamp for the identifier.

A gatekeeper must keep a memory of control messages received from the
master. If a control message exists for the current domain and identifier
with a timestamp after the current timestamp, then the gatekeeper must delay
or reject the request. The decision as to whether to delay or reject can be
made based on the length of the delay (requests that would be delayed for an
unreasonably long time should be rejected instead) or based on resource
considerations (delayed requests consume resources, and a gatekeeper may
choose to reject further requests if these resources are exhausted).

In the context of HTTP, a request is "delayed" by keeping the client
connection open, but delaying the presentation of the request to the
resource until a later time. A request is "rejected" by sending the client a
503 "Service Unavailable" response (optionally with a Retry-After header
indicating a later time at which a request would be acceptable), and
preventing the request from reaching the resource.

A gatekeeper must send an "accepted" accounting message for each request it
accepts, with the identifier and request timestamp.

A gatekeeper must send a "delayed" accounting message for each request it
delays, with the identifier, request timestamp, and timestamp to which the
request was delayed.

A gatekeeper may send a "rejected" accounting message for each request it
rejects, with the identifier and request timestamp. Rejected accounting may
be disabled or sampled if it is desirable to reduce network traffic or
master load.

All accounting messages may have an optional final field which contains
information of use for debugging (for example the client's User-Agent or
Referrer headers). The format is unspecified but should be suitable for
logging.

## Low-Level Proto:

### Accounting Socket

Gatekeeper PUB -> Master tcp:10004 SUB

Master subscribes to "" (all messages)

| domain | status | identifier | receive<br>timestamp | delayed<br>timestamp | loginfo |
|:------:|:------:|:----------:|:--------------------:|:--------------------:|:-------:|
|    0   |    1   |      2     |           3          |         4            | 5       |

#### Frame 0: Domain

The domain should be encoded with a trailing null ("\0"), which will be
stripped off by the recipient. This ensures it is usable as a pubsub key.
Domains should not contain nulls in their names.

#### Frame 1: Status

* "ACCEPTED": Request was accepted
* "DELAYED": Request was delayed
* "REJECTED": Request was rejected

#### Frame 2: Identifier

#### Frame 3: Receive Timestamp

As milliseconds since 1970, formatted as ASCII decimal. The time when the
request was received at the gatekeeper.

#### Frame 4: Delayed Timestamp

For DELAYED messages, the time at which the request was forwarded to the
resource. For ACCEPTED and REJECTED messages, this frame is zero-length.

#### Frame 5: Log Info

Unspecified.

### Control Socket

Gatekeeper SUB -> Master tcp:10005 PUB

Gatekeeper subscribes to all domains for which it will receive traffic.

| domain | command | identifier | arguments... |
|:------:|:-------:|:----------:|:------------:|
|    0   |    1    |      2     |      3+      |

The domain should be encoded with a trailing null ("\0"), which will be
stripped off by the recipient. This ensures it is usable as a pubsub key.
Domains should not contain nulls in their names.

#### DELAY_UNTIL

Instructs gatekeepers that requests from clients identified by `identifier`
should not be allowed before a given time.

* Frame 3: Next request timestamp.

## Fault-Tolerance:

Smockron is resilient against multiple failure scenarios. It attempts to
preserve access to the resources it protects, even at the cost of
potentially allowing some undesired requests to pass; that is, it is a
"fail-permissive" design.

Because the gatekeeper uses local information to decide the disposition of
each request instead of consulting the master, it has minimal impact on the
latency of requests. If the gatekeeper is incorporated as part of an
existing proxy or application then no additional network hops are added in
the request path.

Because it is possible that the gatekeeper itself could have crashing bugs,
it is recommended that it is installed in a part of the system which is not
a single point of failure. Smockron is designed to provide rate-limiting in
multi-machine distributed applications, and the gatekeepers should be
distributed as well. If you prefer to funnel all of your traffic through a
single point in order to rate-limit it, you can probably find something
simpler than Smockron to do it with.

If the master crashes, accounting messages from the gatekeepers will be
dropped, and the gatekeepers will receive no control messages. In the
absence of control messages, the gatekeepers will allow all requests,
preserving access to the underlying resource.

If the master becomes overloaded, it will queue and then drop accounting
messages. If the processing delay for accounting messages is too long, the
master may send control messages too late to be effective. If accounting
messages are dropped, the master may think that the number of requests to a
domain is less than it is. In both cases, requests might be allowed that
would have been dropped in the ideal case of a master that responds
instantly, but no requests will be delayed or rejected that would not have
been otherwise.
