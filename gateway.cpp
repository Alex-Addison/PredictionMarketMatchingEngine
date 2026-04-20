#include <quickfix/Application.h>
#include <quickfix/Values.h>
#include <quickfix/ThreadedSocketAcceptor.h>
#include <quickfix/SocketInitiator.h>
#include <quickfix/FileStore.h>
#include <quickfix/FileLog.h>
#include <quickfix/SessionSettings.h>
#include <quickfix/Session.h>

#include <iostream>
#include <string>
#include <map>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>
#include <ctime>

class GatewayApp : public FIX::Application {
private:
    std::map<std::string, std::string> routingMap;          
    std::map<std::string, FIX::SessionID> engineSessions;
    std::map<std::string, FIX::SessionID> traderSessions; 

public:
    std::atomic<uint64_t> msgCount{0};
    std::atomic<bool> loadTestRunning{false};

    GatewayApp() {
        // get routing info from the routing.txt file
        std::ifstream file("routing.txt");
        std::string line;
        while (std::getline(file, line)) {
            auto delimPos = line.find('=');
            if (delimPos != std::string::npos) {
                routingMap[line.substr(0, delimPos)] = line.substr(delimPos + 1);
            }
        }
    }

    void onCreate(const FIX::SessionID&) override {}
    // establish connections to engines and traders
    void onLogon(const FIX::SessionID& sessionID) override {
        std::string remoteParty = sessionID.getTargetCompID().getString();

        if (remoteParty.find("TRADER") != std::string::npos) {
            traderSessions[remoteParty] = sessionID;
            std::cout << "[GATEWAY] Registered Session: " << remoteParty << std::endl;
        } 
        else if (remoteParty.find("ENGINE") != std::string::npos) {
            engineSessions[remoteParty] = sessionID;
            std::cout << "[GATEWAY] Registered Engine: " << remoteParty << std::endl;
        }
    }
    void onLogout(const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message&, const FIX::SessionID&) throw(FIX::DoNotSend) override {}

    // debug quickFIX protocol issues
    void fromAdmin(const FIX::Message& message, const FIX::SessionID&) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) override {
        
        if (message.getHeader().getField(FIX::FIELD::MsgType) == FIX::MsgType_Reject) {
            std::cout << "\n[ADMIN] QuickFIX dropped a message" << std::endl;
            
            // tag # of the failure
            if (message.isSetField(FIX::FIELD::RefTagID)) {
                std::cout << "[ADMIN] Missing Tag: " << message.getField(FIX::FIELD::RefTagID) << std::endl;
            }
            // print raw fix string to debug
            std::cout << "[ADMIN] Raw FIX String:  " << message.toString() << "\n" << std::endl;
        }
    }

    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) 
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override {
        // set flag that test is running
        loadTestRunning = true;
        msgCount++;

        std::string sender = sessionID.getTargetCompID().getString();
        
        if (sender.find("TRADER") != std::string::npos) {
            std::string symbol = message.getField(FIX::FIELD::Symbol);
            std::string targetEngine = "";

            for (const auto& pair : routingMap) {
                if (symbol.find(pair.first) != std::string::npos) {
                    targetEngine = pair.second;
                    break;
                }
            }

            if (!targetEngine.empty() && engineSessions.count(targetEngine)) {
                FIX::Message msgCopy = message;
                FIX::Session::sendToTarget(msgCopy, engineSessions[targetEngine]);
            } 
            else { // reject bad symbol
                std::cout << "Rejecting Invalid Symbol: " << symbol << std::endl;
                FIX::Message reject;
                reject.getHeader().setField(FIX::BeginString("FIXT.1.1"));
                reject.getHeader().setField(FIX::MsgType(FIX::MsgType_ExecutionReport));
                
                reject.setField(FIX::ClOrdID(message.getField(FIX::FIELD::ClOrdID)));
                reject.setField(FIX::OrderID("NONE"));
                reject.setField(FIX::ExecID("REJ_" + std::to_string(std::time(0))));
                reject.setField(FIX::OrdStatus(FIX::OrdStatus_REJECTED)); 
                reject.setField(FIX::ExecType(FIX::ExecType_REJECTED));
                reject.setField(FIX::Symbol(symbol));
                reject.setField(FIX::Side(message.getField(FIX::FIELD::Side)[0]));
                reject.setField(FIX::LeavesQty(0));
                reject.setField(FIX::CumQty(0));
                
                reject.setField(FIX::TransactTime()); 
                reject.setField(FIX::Text("Gateway: Invalid Symbol"));
                reject.setField(FIX::AvgPx(0.0)); 
                reject.setField(FIX::OrderQty(std::stod(message.getField(FIX::FIELD::OrderQty))));
                FIX::Session::sendToTarget(reject, sessionID);
            }
        } 
        else {
            std::string clOrdID = message.getField(FIX::FIELD::ClOrdID);
            FIX::Message msgCopy = message;
            // send to the right trader
            if (clOrdID.find("TRD1") == 0 && traderSessions.count("TRADER1")) {
                FIX::Session::sendToTarget(msgCopy, traderSessions["TRADER1"]);
            } 
            else if (clOrdID.find("TRD2") == 0 && traderSessions.count("TRADER2")) {
                FIX::Session::sendToTarget(msgCopy, traderSessions["TRADER2"]);
            }
        }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        FIX::SessionSettings settings(argv[1]);
        GatewayApp application;
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);
        FIX::ThreadedSocketAcceptor acceptor(application, storeFactory, settings, logFactory);
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        
        acceptor.start();
        initiator.start();

        std::cout << "[GATEWAY] Running" << std::endl;
        
        uint64_t lastMsgCount = 0;
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (application.loadTestRunning) {
                uint64_t currentTotal = application.msgCount.load();
                uint64_t rollingThroughput = (currentTotal - lastMsgCount)*10;
                lastMsgCount = currentTotal;

                // write to file for the python widget
                std::ofstream metricFile("metrics.txt", std::ios::trunc); 
                if (metricFile.is_open()) {
                    metricFile << rollingThroughput;
                    metricFile.close();
                }
            }
        }
        initiator.stop();
        acceptor.stop();
    } catch (FIX::Exception& e) {
        std::cout << e.what() << std::endl;
    }
    return 0;
}