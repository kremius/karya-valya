#pragma once

#include "OnMapInt.h"

class IMessageReceiver : public IOnMapItem
{
public:
    DECLARE_GET_TYPE_ITEM(IMessageReceiver);
    DECLARE_SAVED(IMessageReceiver, IOnMapItem);
    virtual void processGUImsg(std::string& msg){};
    IMessageReceiver(){}
};

ADD_TO_TYPELIST(IMessageReceiver);