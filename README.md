# Prediction Market Matching Engine

A distributed matching engine for prediction markets (like kalshi, polymarket, robinhood, etc.). The system processes order streams and matches bids and asks using a pricetime priority algorithm. Emphasis on speed and accuracy, so that the market is “fair” for all participants.

(a) What the project does 

The project is a distributed system with 5 nodes: 2 trader nodes, a gateway, and 2 separate engine nodes. The node behavior is defined by each respective .cpp file. The project successfully creates and connects FIX sessions between all the nodes, and runs 5 tests.

Current Order of Events: 
Spin up engine 1 and 2 
Spin up gateway 
Start trader1
Start trader2
Trader1: 
- Run Invalid Symbol Test
- Run Report Test
- Run Scaling test
- Sync with Trader 2
Trader1 and Trader2 (Simultaneously):
- Run the matching trades test
- Run the deep book test
Gateway: 
- Route all trades to their destination
- Write rolling thorughput to metrics.txt for use by the accompanying widget
Engines: 
- Receive and book test trades 
- Generate and send reports to gateway 

The project only supports limit orders, but market orders can be emulated by selling for $0.00 or buying for $1.00 (the minumum and maximum prices for event contracts). The project sets up all of the necessary communication between nodes and works on the Fabric testbed. The setup.ipynb has the code necessary to deploy to FABRIC.

This is an example of how routing works in the project (showing only 1 trader for simplicity):

<img width="1138" height="487" alt="Screenshot 2026-03-29 231717" src="https://github.com/user-attachments/assets/60847058-3ef9-4e67-b7ca-dd36855fbdaa" />

The project also comes with a python notebook widget to monitor the rolling throughput of the gateway node:

<img width="1666" height="654" alt="image" src="https://github.com/user-attachments/assets/80bd5d9e-d48d-47f3-9b63-82942839510e" />

(b) How to use your implementation

For localhost testing:

Pull the code from repo, directory structure should look something like this: 

<img width="757" height="777" alt="Screenshot 2026-03-29 234600" src="https://github.com/user-attachments/assets/0c622f74-51cc-421c-bbe3-20557c82fb78" />

Ensure you are in a linux environment (I used WSL with ubuntu)
Run `sudo apt-get update -y -qq && sudo apt-get install -y build-essential cmake libquickfix-dev`
Then run `make all` in the terminal
In each config file, ensure that you remove the subnet IPs, as these are for fabric deployment.
example of line to remove in gateway.cfg:
<img width="484" height="328" alt="image" src="https://github.com/user-attachments/assets/9a0b3683-4a03-4711-9c0a-11318de6ba71" />
Once everything has compiled, you need to open 5 seperate terminal instances (or you can run procs in the background)
Start the processes in this order, using these commands:
`./engine1 engine1.cfg`
`./engine2 engine2.cfg`
`./gateway gateway.cfg`
`./trader trader1.cfg`
`./trader trader2.cfg`

This will start all 5 nodes, sending logon messages and heartbeats between the fix engines. To stop the programs, use ctrl-c in each terminal window.

For testing on fabric (recommended):

Run the provided setup.ipynb:

1. Create the slice
2. Install C++ and Quickfix
3. Create subnet and assign IPs
4. (optional) test connection by pinging nodes from gateway
5. Upload files to every node and compile

Then open 5 terminals, connecting to each node over ssh, and run each command on its respective node in order:
`./engine1 engine1.cfg`
`./engine2 engine2.cfg`
`./gateway gateway.cfg`
`./trader trader1.cfg`
`./trader trader2.cfg`

(Optional) To use the throughputWidget.ipynb visualizer:
1. run the cell in throughputWidget.ipynb
2. wait for gateway to begin receiving trades
3. observe

Whether you run on fabric or localhost, you can also look in store/logs to view the messages sent between all of the nodes.

Example log:
```
20260330-04:31:13.892005000 : Logon contains ResetSeqNumFlag=Y, reseting sequence numbers to 1
20260330-04:31:13.929483000 : Received logon request
20260330-04:31:13.932004000 : Responding to logon request
20260330-04:31:41.534252000 : Initiated logout request
20260330-04:31:41.539973000 : Received logout response
20260330-04:31:41.540464000 : Disconnecting
```

(c) Limitations of your implementation

The implementation is currently hardcoded to work with only 3 markets (KXFED, KXWEATHER, and the SYNC-MARKET), and it has two simulated traders. The main limitation is consistent speed, as we can see that the average and minimum latencies are both quite fast (sub 1-ms), the max latency is 1 - 2 orders of magnitude longer: 

<img width="656" height="216" alt="image" src="https://github.com/user-attachments/assets/d7f6994d-85ae-4f1f-a1de-b23e3e447f11" />

<img width="501" height="225" alt="image" src="https://github.com/user-attachments/assets/021ad30f-a934-4801-a5a2-8f830be1b923" />

Even though the order book is optimized by using atomic arrays and circular buffers instead of things like maps and vectors, there is still a bottleneck when it becomes very full. Additionally, the implementation uses C++14 and cannot be updated due to compatibility issues with quickFIX, the FIX library used (this also has the annoying effect of raising the compilation errors shown below):

<img width="1664" height="385" alt="image" src="https://github.com/user-attachments/assets/477736a3-7bf3-4a9e-a762-ebf2846b0ec5" />


 (d) Future updates

 Here is a list of additional features that I am hoping to implement in newer versions:

- Speed optimizations by multithreading the gateway, or using raw message passing at gateway instead of FIX parsing
- Additional Simulated Trader Nodes
- Fill or Kill (FoK) and Immediate or Cancel (IoC) order support
- Market creation/settlement widget (spin up new engines and kill old ones automatically)
- Order Book visualization
