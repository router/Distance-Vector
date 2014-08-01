Distance-Vector
===============

A simplified implementation of the Distance Vector Protocol. The protocol runs on top of servers (behaving as routers) using UDP. Each server runs on a machine at a pre-defined port number. The servers are able to output their forwarding tables along with the link costs and are robust to link changes. The current implementation faces the Count-to-infinity problem.
