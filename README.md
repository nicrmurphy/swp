# swp
Implementation of Sliding Window Protocols

## TODO
- [ ] Reliable communication between two processes, e.g., A and B, with error-handling using the Internet Checksum or the CRC checksum algorithm
  - [x] Datagram socket communication
    - [x] Simple communication (strings)
    - [x] FTP for any (one) client to any (one) server
    - [ ] _Optional: FTP for multiple clients to multiple servers_
  - [x] Error Detection
  - [x] Dynamic packet size
  - [x] Dynamic timeout interval
  - [x] Dynamic window size
  - [x] Dynamic range of sequence numbers
  - [x] Simulate packet loss
    - [x] Randomly generated
    - [x] User-defined
  - [ ] Simulate damaged packet

- [ ] Helpers
  - [x] Checksum
  - [x] Create frame
  - [x] Create ack
    - [x] ~~Create nak~~
  - [x] Read frame
  - [x] Read ack

- [x] Sliding Window Protocols
  - [x] ~~Stop and Wait Protocol?~~
  - [x] Go Back N Protocol
  - [x] Selective Repeat Protocol

- [ ] GUI
  - [ ] Server
    - [x] User input
      - [x] Select Protocol
      - [x] Packet Size
      - [x] Timeout Interval (user-specified or ping-calculated)
      - [x] Window Size
      - [x] Range of Sequence Numbers
      - [1] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [ ] Console output
      - [x] Packets (sequence number) received
      - [ ] Damaged packet(s) (checksum error should match corresponding damaged packet from sender window)
      - [x] Packets in the current receiver window.
      - [ ] Packets (duplicated) that are discarded
      - [x] Ack packets that are sent
      - [ ] Frames arriving out of order should be re-sequenced before assembly
  - [ ] Client
    - [ ] User input
      - [x] Server Host
      - [x] Select Protocol
      - [x] Packet Size
      - [x] Timeout Interval (user-specified or ping-calculated)
      - [x] Window Size
      - [x] Range of Sequence Numbers
      - [ ] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [ ] Console output
      - [x] Packet sequence number of packet sent
      - [x] Packet (sequence number) that timed out
      - [x] Packet that was re-transmitted
      - [x] All packets in the current window
      - [x] Acks received
      - [ ] Which packets are damaged, .i.e., deliberately trigger a checksum error on the receiver side

- [ ] Design document, describing and justifying design decisions
