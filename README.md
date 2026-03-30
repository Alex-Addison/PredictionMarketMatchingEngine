# Prediction Market Matching Engine

A distributed matching engine for prediction markets (like kalshi, polymarket, robinhood, etc.). The system will process order streams and match bids and asks using a pricetime priority algorithm. Emphasis on speed and accuracy, so that the market is “fair” for all participants.

(a) What the project does 

As of now, the project is barebones distributed system with 4 nodes: A trader, gateway, and 2 separate engine nodes. The node behavior is defined by each respective .cpp file. The project successfully creates and connects FIX sessions between all the nodes, and sends 3 test trade inputs. 

Current Order of Events: 
Spin up engine 1 and 2 
Spin up gateway 
Start simulated trader 
Trader: 
- Send Test Trade 1 to Gateway 
- Send Test Trade 2 to Gateway 
- Send Test Trade 3 to Gateway 
Gateway: 
- Route Test Trade 1 to engine 1 
- Route Test Trade 2 & 3 to engine 2 
Engines: 
- Receive and book test trades 
- Generate and send reports to gateway 
Gateway Routes reports to specified trader 

The project only supports limit orders so far and does not fill orders yet. The project sets up all of the necessary communication between nodes and works on localhost in a WSL environment (Launching the nodes from 4 separate terminals). The setup.ipynb has the code necessary to deploy the current config to FABRIC.

This is an example of how routing works in the project:

<img width="1138" height="487" alt="Screenshot 2026-03-29 231717" src="https://github.com/user-attachments/assets/60847058-3ef9-4e67-b7ca-dd36855fbdaa" />

(b) How to use your implementation

For localhost testing:

Pull the code from repo, directory structure should look something like this: 

<img width="757" height="777" alt="Screenshot 2026-03-29 234600" src="https://github.com/user-attachments/assets/0c622f74-51cc-421c-bbe3-20557c82fb78" />

Ensure you are in a linux environment (I used WSL with ubuntu)
Run `sudo apt-get update -y -qq && sudo apt-get install -y build-essential cmake libquickfix-dev`
Then run `make all` in the terminal
Once everything has compiled, you need to open 4 seperate terminal instances (or you can run procs in the background)
Start the processes in this order, using these commands:
`./engine1 engine1.cfg`
`./engine2 engine2.cfg`
`./gateway gateway.cfg`
`./trader trader.cfg`

This will start all 4 nodes, sending logon messages and heartbeats between the fix engines. You should see output from the trader program that 3 trades were sent, and 3 trade reports were received. To stop the programs, just press enter in each terminal window.

You can look in store/logs to view the messages sent between all of the nodes.

Example log:
```
20260330-04:31:13.892005000 : Logon contains ResetSeqNumFlag=Y, reseting sequence numbers to 1
20260330-04:31:13.929483000 : Received logon request
20260330-04:31:13.932004000 : Responding to logon request
20260330-04:31:41.534252000 : Initiated logout request
20260330-04:31:41.539973000 : Received logout response
20260330-04:31:41.540464000 : Disconnecting
```

For testing on fabric, run the provided setup.ipynb, and change/add the `SocketConnectHost=127.0.0.1` ling in the .cfg files so that the IPs match what is assigned to each fabric node.

(c) Limitations of your implementation

The implementation is currently hardcoded to work with only 2 markets (KXFED, KXWEATHER), and it only has one simulated trader. The main limitation is the lack of order filling logic within each engine, so trades are not actually executed. Also, the implementation uses C++14 and cannot be updated due to compatibility issues with quickFIX, the FIX library used.

 (d) TODO

 Here is a list of additional features in order of importance:

 1. Matching Algorithm (Actually fill orders using a price-time priority algorithm)
 2. Market order support (Allow traders to buy or sell at the current market price)
 3. Additional Simulated Trader Nodes
 4. Jupyterhub widget for manual test trade submission
 5. Fill or Kill (FoK) and Immediate or Cancel (IoC) order support
 6. Market creation/settlement widget (spin up new engines and kill old ones automatically)
 7. Order Book visualization
