# Prediction Market Matching Engine

A distributed matching engine for prediction markets (like kalshi, polymarket, robinhood, etc.). The system will process order streams and match bids and asks using a pricetime priority algorithm. Emphasis on speed and accuracy, so that the market is “fair” for all participants.

The system consists of 3 nodes, whose behavior is determined by their respective *.cpp files.

trader.cpp - acts as a simulated trader, sending trades to the gateway using the quickFIX engine
gateway.cpp - parses the market that the trade belongs too, and routes the trade accordingly
engine.cpp - matches the trade with another from the LOB


