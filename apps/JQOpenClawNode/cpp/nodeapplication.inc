#ifndef JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_INC_
#define JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_INC_

// .h include
#include "nodeapplication.h"

// Property accomplish code start
inline NodeApplication::ConnectionState NodeApplication::connectionState() const
{ return connectionState_; }
inline void NodeApplication::setConnectionState(const ConnectionState &newValue)
{ if ( newValue == connectionState_ ) { return; } connectionState_ = newValue; emit connectionStateChanged( connectionState_ ); }
inline QString NodeApplication::connectionStateText() const
{ return connectionStateText_; }

inline QString NodeApplication::lastInvokeTime() const
{ return lastInvokeTime_; }
inline void NodeApplication::setLastInvokeTime(const QString &newValue)
{ if ( newValue == lastInvokeTime_ ) { return; } lastInvokeTime_ = newValue; emit lastInvokeTimeChanged( lastInvokeTime_ ); }

inline QJsonObject NodeApplication::config() const
{ return config_; }
inline void NodeApplication::setConfig(const QJsonObject &newValue)
{
    const QJsonObject normalizedConfig = normalizeConfig(newValue);
    if ( normalizedConfig == config_ ) { return; }
    config_ = normalizedConfig;
    emit configChanged( config_ );
}
// Property accomplish code end

#endif // JQOPENCLAW_APPS_JQOPENCLAWNODE_NODEAPPLICATION_INC_
