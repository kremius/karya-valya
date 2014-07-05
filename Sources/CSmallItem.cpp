#include "NetClientImpl.h"
#include "CSmallItem.h"
#include "MapClass.h"
#include "sync_random.h"
#include "ItemFabric.h"

CMeat::CMeat()
{
    SetSprite("icons/meat.png");
    name = "Meat";
};

void CWeed::process()
{
    SmallItem::process();
};

CWeed::CWeed()
{
    v_level = 2;
    SetSprite("icons/kivtek.png");
//    imageStateH = get_rand() % 2;
//    imageStateW = get_rand() % 2;
    name = "Weed";
};

CupItem::CupItem()
{
    SetSprite("icons/cup.png");
    name = "Cup";
}