#include "ConnectionManager.h"


ConnectionManager ConnectionManager::m_instance;

ConnectionManager& ConnectionManager::instance() 
{
    return m_instance;
}

bool ConnectionManager::canAcceptNewConnection() const 
{
    return activeConnections_.load() < maxConnections_;
}

void ConnectionManager::connectionEstablished() 
{ 
    ++ activeConnections_; 
}

void ConnectionManager::connectionFinished() 
{ 
    -- activeConnections_; 
}

void ConnectionManager::setMaxConnections(int max) 
{ 
    maxConnections_ = max; 
}

int ConnectionManager::activeConnections() const 
{ 
    return activeConnections_.load(); 
}