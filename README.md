# swp
Implementation of Sliding Window Protocols

## TODO
- [ ] Reliable communication between two processes, e.g., A and B, with error-handling using the Internet Checksum or the CRC checksum algorithm
  - [x] Datagram socket communication
    - [x] Simple communication (strings)
    - [x] FTP for any (one) client to any (one) server
    - [ ] _Optional: FTP for multiple clients to multiple servers_
  - [ ] Error Detection
  - [ ] Dynamic packet size
  - [ ] Dynamic timeout interval
  - [ ] Dynamic window size
  - [ ] Dynamic range of sequence numbers
  - [ ] Simulate packet loss
    - [ ] Randomly generated
    - [ ] User-defined

- [ ] Helpers
  - [ ] Checksum
  - [ ] Create frame
  - [ ] Create ack
    - [ ] ~~Create nak~~
  - [ ] Read frame
  - [ ] Read ack

- [ ] Sliding Window Protocols
  - [ ] ~~Stop and Wait Protocol?~~
  - [ ] Go Back N Protocol
  - [ ] Selective Repeat Protocol

- [ ] GUI
  - [ ] Server
    - [ ] User input
      - [ ] Select Protocol
      - [ ] Packet Size
      - [ ] Timeout Interval (user-specified or ping-calculated)
      - [ ] Window Size
      - [ ] Range of Sequence Numbers
      - [ ] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [ ] Console output
      - [ ] Packets (sequence number) received
      - [ ] Damaged packet(s) (checksum error should match corresponding damaged packet from sender window)
      - [ ] Packets in the current receiver window. o Packets (duplicated) that are discarded
      - [ ] Ack packets that are sent
      - [ ] Frames arriving out of order should be re-sequenced before assembly
  - [ ] Client
    - [ ] User input
      - [ ] Server Host
      - [ ] Select Protocol
      - [ ] Packet Size
      - [ ] Timeout Interval (user-specified or ping-calculated)
      - [ ] Window Size
      - [ ] Range of Sequence Numbers
      - [ ] Situational Errors (none, randomly generated, or user-specified, i.e., drop packets 2, 4, 5, lose acks 11, etc.)
    - [ ] Console output
      - [ ] Packet sequence number of packet sent
      - [ ] Packet (sequence number) that timed out
      - [ ] Packet that was re-transmitted
      - [ ] All packets in the current window
      - [ ] Acks received
      - [ ] Which packets are damaged, .i.e., deliberately trigger a checksum error on the receiver side

- [ ] Design document, describing and justifying design decisions
