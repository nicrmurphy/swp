# swp
Implementation of Sliding Window Protocols

## TODO
- [x] Reliable communication between two processes, e.g., A and B, with error-handling using the Internet Checksum or the CRC checksum algorithm
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
  - [x] Simulate damaged packet

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

- [x] GUI
  - [x] Server
    - [x] User input
      - [x] Select Protocol
      - [x] Packet Size
      - [x] Timeout Interval (user-specified or ping-calculated)
      - [x] Window Size
      - [x] Range of Sequence Numbers
      - [1] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [x] Console output
      - [x] Packets (sequence number) received
      - [x] Damaged packet(s) (checksum error should match corresponding damaged packet from sender window)
      - [x] Packets in the current receiver window.
      - [x] Packets (duplicated) that are discarded
      - [x] Ack packets that are sent
      - [x] Frames arriving out of order should be re-sequenced before assembly
  - [x] Client
    - [x] User input
      - [x] Server Host
      - [x] Select Protocol
      - [x] Packet Size
      - [x] Timeout Interval (user-specified or ping-calculated)
      - [x] Window Size
      - [x] Range of Sequence Numbers
      - [x] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [x] Console output
      - [x] Packet sequence number of packet sent
      - [x] Packet (sequence number) that timed out
      - [x] Packet that was re-transmitted
      - [x] All packets in the current window
      - [x] Acks received
      - [x] Which packets are damaged, .i.e., deliberately trigger a checksum error on the receiver side

- [x] Design document, describing and justifying design decisions
