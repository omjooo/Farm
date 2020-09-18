#include "FarmScene.h"
#include "SoilLayer.h"
#include "CropLayer.h"
#include "EffectLayer.h"
#include "Soil.h"
#include "Crop.h"
#include "DynamicData.h"
#include "StaticData.h"
#include "Seed.h"
#include "SliderDialog.h"
#include "TextDialog.h"
#include "Entity.h"
#include "Toast.h"

FarmScene::FarmScene()
	:m_pSoilLayer(nullptr)
	,m_pCropLayer(nullptr)
	,m_pEffectLayer(nullptr)
	,m_pFarmUILayer(nullptr)
	,m_pGoodLayer(nullptr)
	,m_nCurPage(0)
	,m_goodLayerType(GoodLayerType::None)
	,m_pSliderDialog(nullptr)
	,m_pTextDialog(nullptr)
	,m_pSelectedSoil(nullptr)
	,m_pSelectedGood(nullptr)
{
}

FarmScene::~FarmScene()
{
	for (auto it = m_shopList.begin(); it != m_shopList.end(); it++)
	{
		auto good = *it;
		SDL_SAFE_RELEASE(good);
	}
	m_shopList.clear();
	SDL_SAFE_RELEASE_NULL(m_pSelectedGood);
}

bool FarmScene::init()
{
	Size visibleSize = Director::getInstance()->getVisibleSize();

	this->preloadResources();

	//创建土壤层
	m_pSoilLayer = SoilLayer::create();
	this->addChild(m_pSoilLayer);
	//创建作物层
	m_pCropLayer = CropLayer::create();
	this->addChild(m_pCropLayer);
	//特效层
	m_pEffectLayer = EffectLayer::create();
	this->addChild(m_pEffectLayer);
	//可扩展土地
	m_pBrandSprite = Sprite::createWithSpriteFrameName("farm_ui_tag_bg2.png");
	this->addChild(m_pBrandSprite);
	//ui层
	m_pFarmUILayer = FarmUILayer::create();
	m_pFarmUILayer->setDelegate(this);
	this->addChild(m_pFarmUILayer);
	//物品层
	m_pGoodLayer = GoodLayer::create();
	m_pGoodLayer->setDelegate(this);
	//默认物品层不可显示
	m_pGoodLayer->setPositionY(-visibleSize.height);
	m_pGoodLayer->updateShowingBtn(BtnType::Equip, BtnParamSt(false, false));
	this->addChild(m_pGoodLayer);
	//滑动条对话框
	m_pSliderDialog = SliderDialog::create();
	m_pSliderDialog->setPosition(visibleSize.width * 0.5f, visibleSize.height * 0.5f);
	m_pSliderDialog->setVisible(false);
	m_pSliderDialog->setCallback(SDL_CALLBACK_2(FarmScene::sliderDialogCallback, this));
	this->addChild(m_pSliderDialog);
	//文本对话框
	m_pTextDialog = TextDialog::create();
	m_pTextDialog->setPosition(visibleSize.width / 2, visibleSize.height / 2);
	m_pTextDialog->setVisible(false);
	m_pTextDialog->setCallback(SDL_CALLBACK_1(FarmScene::tryBuyingSoilCallback, this));
	this->addChild(m_pTextDialog);

	//初始化土壤和作物
	this->initializeSoilsAndCrops();
	//初始化商店
	this->initializeShopGoods();
	//确认可扩展土地精灵的位置
	auto soilID = this->getValueOfKey(FARM_EXTENSIBLE_SOIL_KEY).asInt();
	if (soilID == 0)
	{
		m_pBrandSprite->setVisible(false);
	}
	else
	{
		auto pos = m_pSoilLayer->getSoilPositionByID(soilID);
		m_pBrandSprite->setPosition(pos);
	}
	//更新数据显示
	int gold = this->getValueOfKey(GOLD_KEY).asInt();
	int lv = this->getValueOfKey(FARM_LEVEL_KEY).asInt();
	int exp = this->getValueOfKey(FARM_EXP_KEY).asInt();
	int maxExp = DynamicData::getInstance()->getFarmExpByLv(lv);

	//更新金币显示
	m_pFarmUILayer->updateShowingGold(gold);
	m_pGoodLayer->updateShowingGold(gold);

	m_pFarmUILayer->updateShowingLv(lv);
	m_pFarmUILayer->updateShowingExp(exp, maxExp);
	//添加事件监听器
	auto listener = EventListenerTouchOneByOne::create();
	listener->onTouchBegan = SDL_CALLBACK_2(FarmScene::handleTouchEvent, this);

	_eventDispatcher->addEventListener(listener, this);
	//开始update函数
	this->scheduleUpdate();

	return true;
}

void FarmScene::update(float dt)
{
	m_pCropLayer->update(dt);
	m_pFarmUILayer->update(dt);
}

bool FarmScene::handleTouchEvent(Touch* touch, SDL_Event* event)
{
	auto location = touch->getLocation();
	//是否点击了土地
	auto soil = m_pSoilLayer->getClickingSoil(location);

	//点到了“空地”
	if (soil == nullptr)
	{
		m_pFarmUILayer->hideOperationBtns();
		//是否点击了扩展面板 购买土地
		auto rect = m_pBrandSprite->getBoundingBox();
		if (m_pBrandSprite->isVisible() 
		 && rect.containsPoint(location))
		{
			string content = STATIC_DATA_STRING("extensible_format");
			auto soilID = this->getValueOfKey(FARM_EXTENSIBLE_SOIL_KEY).asInt();
			//当前购买的是第几块土地
			int id = 12 - soilID;
			//获取结构体
			auto pExtensibleSoilSt = StaticData::getInstance()->getExtensibleSoilStructByID(id);
			content = StringUtils::format(content.c_str()
				, pExtensibleSoilSt->lv, pExtensibleSoilSt->value);

			m_pTextDialog->setVisible(true);
			m_pTextDialog->setShowing(true);
			m_pTextDialog->updateShowingTitle(STATIC_DATA_STRING("extensible_text"));
			m_pTextDialog->updateShowingContent(content);
		}
		return true;
	}
	//获取土壤对应的作物
	auto crop = soil->getCrop();

	//未种植作物,呼出背包
	if (crop == nullptr)
	{
		m_pFarmUILayer->hideOperationBtns();
		//记忆土壤
		m_pSelectedSoil = soil;
		//显示种子背包
		this->showSeedBag();
	}
	else//存在作物，显示操作按钮
	{
		m_pFarmUILayer->showOperationBtns(crop);
	}
	return false;
}

void FarmScene::harvestCrop(Crop* crop)
{
	auto dynamicData = DynamicData::getInstance();
	//隐藏操作按钮
	m_pFarmUILayer->hideOperationBtns();

	//果实个数
	int number = crop->harvest();
	int id = crop->getCropID();
	//获取果实经验
	auto pCropSt = StaticData::getInstance()->getCropStructByID(id);
	//获取经验值和等级
	Value curExp = this->getValueOfKey(FARM_EXP_KEY);
	Value lv = this->getValueOfKey(FARM_LEVEL_KEY);
	int allExp = dynamicData->getFarmExpByLv(lv.asInt());

	curExp = pCropSt->exp + curExp.asInt();
	//是否升级
	if (curExp.asInt() >= allExp)
	{
		curExp = curExp.asInt() - allExp;
		lv = lv.asInt() + 1;
		allExp = dynamicData->getFarmExpByLv(lv.asInt());
		//更新控件
		m_pFarmUILayer->updateShowingLv(lv.asInt());
		//等级写入
		dynamicData->setValueOfKey(FARM_LEVEL_KEY, lv);
	}
	m_pFarmUILayer->updateShowingExp(curExp.asInt(), allExp);
	//果实写入
	dynamicData->addGood(GoodType::Fruit, StringUtils::toString(id), number);
	//经验写入
	dynamicData->setValueOfKey(FARM_EXP_KEY, curExp);
	//作物季数写入
	dynamicData->updateCrop(crop);
}

void FarmScene::shovelCrop(Crop* crop)
{
	//隐藏操作按钮
	m_pFarmUILayer->hideOperationBtns();
	SDL_SAFE_RETAIN(crop);
	m_pCropLayer->removeCrop(crop);
	DynamicData::getInstance()->shovelCrop(crop);
	//设置土壤
	auto soil = crop->getSoil();
	soil->setCrop(nullptr);
	crop->setSoil(nullptr);

	SDL_SAFE_RELEASE(crop);
}

void FarmScene::fightCrop(Crop* crop)
{
}

void FarmScene::showWarehouse()
{
	m_goodLayerType = GoodLayerType::Warehouse;
	auto pBagGoodList = DynamicData::getInstance()->getBagGoodList();

	this->showGoodLayer("bag_title_txt1.png", "sell_text.png", pBagGoodList, m_nCurPage);
}

void FarmScene::showShop()
{
	this->setVisibleofGoodLayer(true);
	m_goodLayerType = GoodLayerType::Shop;
	//填充商店物品
	this->showGoodLayer("bag_title_txt1.png", "buy_text.png", &m_shopList, m_nCurPage);
}

void FarmScene::showSeedBag()
{
	//TODO:暂时显示的和背包相同
	m_goodLayerType = GoodLayerType::SeedBag;
	auto pBagGoodList = DynamicData::getInstance()->getBagGoodList();

	this->showGoodLayer("bag_title_txt1.png", "plant_text.png", pBagGoodList, m_nCurPage);
}

void FarmScene::saveData()
{
	DynamicData::getInstance()->save();
	auto text = STATIC_DATA_STRING("save_success_text");
	Toast::makeText(this, text, Color3B(255, 255, 255), 1.f);
}

void FarmScene::pageBtnCallback(GoodLayer* goodLayer, int value)
{
	//总页码
	int size = 0;

	vector<Good*>* pBagGoodList = nullptr;

	if (m_goodLayerType == GoodLayerType::Shop)
		pBagGoodList = &m_shopList;
	else if (m_goodLayerType == GoodLayerType::Warehouse)
		pBagGoodList = DynamicData::getInstance()->getBagGoodList();
	else if (m_goodLayerType == GoodLayerType::SeedBag)
		pBagGoodList = DynamicData::getInstance()->getBagGoodList();

	size = pBagGoodList->size();
	int totalPage = size / 4;
	if (size % 4 != 0)
		totalPage += 1;

	m_nCurPage += value;
	//越界处理
	if (m_nCurPage <= 0)
		m_nCurPage = totalPage;
	else if (m_nCurPage > totalPage)
		m_nCurPage = 1;
	//切片处理
	vector<GoodInterface*> content;
	for (int i = 0; i < 4; i++)
	{
		int index = (m_nCurPage - 1) * 4 + i;
		
		if (index >= size)
			break;

		content.push_back(pBagGoodList->at(index));
	}

	m_pGoodLayer->updateShowingPage(m_nCurPage, totalPage);
	m_pGoodLayer->updateShowingGoods(content);
}

void FarmScene::useBtnCallback(GoodLayer* goodLayer)
{
	auto dynamicData = DynamicData::getInstance();
	if (m_pSelectedGood == nullptr)
	{
		printf("m_pSelectedGood == nullptr\n");
		return ;
	}
	//出售
	if (m_goodLayerType == GoodLayerType::Warehouse)
	{
		int number = m_pSelectedGood->getNumber();
		int gold = m_pSelectedGood->getCost();
		//填充
		m_pSliderDialog->setVisible(true);
		m_pSliderDialog->setShowing(true);

		m_pSliderDialog->setPrice(gold);
		m_pSliderDialog->setMaxPercent(number);
		//设置当前进度值为1
		m_pSliderDialog->setPercent(1);
		m_pSliderDialog->updateShowingTitle(m_pSelectedGood->getName());
	}
	//购买物品
	else if (m_goodLayerType == GoodLayerType::Shop)
	{
		//判断等级
		string goodName = m_pSelectedGood->getGoodName();
		int id = atoi(goodName.c_str());
		auto pCropSt = StaticData::getInstance()->getCropStructByID(id);
		auto lv = this->getValueOfKey(FARM_LEVEL_KEY).asInt();
		if (lv < pCropSt->level)
		{
			auto text = STATIC_DATA_STRING("not_enough_lv_text");
			Toast::makeText(this, text, Color3B(255, 255, 255), 1.f);
			return ;
		}
		//呼出滑动条对话框
		Value gold = this->getValueOfKey(GOLD_KEY);
		int cost = m_pSelectedGood->getCost();
		int maxPercent = gold.asInt() / cost;
		//一个都买不起，提示
		if (maxPercent == 0)
		{
			auto text = STATIC_DATA_STRING("not_enough_money_text");
			Toast::makeText(this, text, Color3B(255, 255, 255), 1.f);
			return ;
		}

		m_pSliderDialog->setVisible(true);
		m_pSliderDialog->setShowing(true);

		m_pSliderDialog->setPrice(cost);
		m_pSliderDialog->setMaxPercent(maxPercent);
		//设置当前进度为1
		m_pSliderDialog->setPercent(1);
		m_pSliderDialog->updateShowingTitle(m_pSelectedGood->getName());
	}
	else if (m_goodLayerType == GoodLayerType::SeedBag)
	{
		//先判断类型是否合法
		if (m_pSelectedGood->getGoodType() != GoodType::Seed)
		{
			auto text = STATIC_DATA_STRING("none_seed_text");
			Toast::makeText(this, text, Color3B(255, 255, 255), 1.f);
			return ;
		}
		//新建作物对象
		int cropID = atoi(m_pSelectedGood->getGoodName().c_str());
		int harvestCount = 1;
		float rate = 0.f;

		Crop* crop = m_pCropLayer->addCrop(cropID, time(NULL), harvestCount, rate);
		crop->setSoil(m_pSelectedSoil);
		m_pSelectedSoil->setCrop(crop);
		//设置位置
		crop->setPosition(m_pSelectedSoil->getPosition());
		//保存当前的植物
		dynamicData->updateCrop(crop);
		int number = m_pSelectedGood->getNumber();
		number--;
		//减少物品
		dynamicData->subGood(m_pSelectedGood, 1);
		//解除引用，不解除亦可
		if (number == 0)
			SDL_SAFE_RELEASE_NULL(m_pSelectedGood);
		//更新显示
		auto pBagGoodList = DynamicData::getInstance()->getBagGoodList();
		this->showGoodLayer("bag_title_txt1.png", "plant_text.png", pBagGoodList, m_nCurPage);
		//关闭菜单
		this->closeBtnCallback(m_pGoodLayer);
	}
}

void FarmScene::equipBtnCallback(GoodLayer* goodLayer)
{
}

void FarmScene::closeBtnCallback(GoodLayer* goodLayer)
{
	this->setVisibleofGoodLayer(false);
}

void FarmScene::selectGoodCallback(GoodLayer* goodLayer, GoodInterface* good)
{
	auto selectedGood = static_cast<Good*>(good);
	SDL_SAFE_RETAIN(selectedGood);
	//设置当前选中物品
	SDL_SAFE_RELEASE_NULL(m_pSelectedGood);
	m_pSelectedGood = selectedGood;
}

void FarmScene::sliderDialogCallback(bool ret, int percent)
{
	m_pSliderDialog->setVisible(false);
	m_pSliderDialog->setShowing(false);
	//点击了取消按钮 操作物品为0
	if (!ret || percent == 0)
		return;

	auto dynamicData = DynamicData::getInstance();
	Value gold = this->getValueOfKey(GOLD_KEY);
	//仓库 出售
	if (m_goodLayerType == GoodLayerType::Warehouse)
	{
		int number = m_pSelectedGood->getNumber();
		gold = gold.asInt() + m_pSelectedGood->getCost() * percent;
		number -= percent;
		//减少物品
		dynamicData->subGood(m_pSelectedGood, percent);
		//解除引用
		if (number == 0)
		{
			SDL_SAFE_RELEASE_NULL(m_pSelectedGood);
		}
		//更新显示 可能会改变m_pSelectedGood
		vector<Good*>* pBagGoodList = DynamicData::getInstance()->getBagGoodList();
		this->showGoodLayer("bag_title_txt1.png", "sell_text.png", pBagGoodList, m_nCurPage);
	}
	//商店 购买
	else if (m_goodLayerType == GoodLayerType::Shop)
	{
		string goodName = m_pSelectedGood->getGoodName();
		auto type = m_pSelectedGood->getGoodType();

		gold = gold.asInt() - m_pSelectedGood->getCost() * percent;
		//增加物品
		dynamicData->addGood(type, goodName, percent);
	}
	//金币回写
	dynamicData->setValueOfKey(GOLD_KEY, gold);
	//更新金币显示
	m_pFarmUILayer->updateShowingGold(gold.asInt());
	m_pGoodLayer->updateShowingGold(gold.asInt());
}

void FarmScene::tryBuyingSoilCallback(bool ret)
{
	m_pTextDialog->setVisible(false);
	m_pTextDialog->setShowing(false);

	if (!ret)
		return ;
	auto dynamicData = DynamicData::getInstance();

	int soilID = this->getValueOfKey(FARM_EXTENSIBLE_SOIL_KEY).asInt();
	Value gold = this->getValueOfKey(GOLD_KEY);
	int lv = this->getValueOfKey(FARM_LEVEL_KEY).asInt();
	//当前购买的是第几块土地
	int id = 12 - soilID;
	//获取结构体
	auto pExtensibleSoilSt = StaticData::getInstance()->getExtensibleSoilStructByID(id);
	//是否满足限制条件
	if (lv < pExtensibleSoilSt->lv || gold.asInt() < pExtensibleSoilSt->value)
	{
		string text;
		if (lv < pExtensibleSoilSt->lv)
			text = STATIC_DATA_STRING("not_enough_lv_text");
		if (gold.asInt() < pExtensibleSoilSt->value)
			text = STATIC_DATA_STRING("not_enough_money_text");

		Toast::makeText(this, text, Color3B(255, 255, 255), 1.f);
		return ;
	}
	//减少金币
	gold = gold.asInt() - pExtensibleSoilSt->value;
	dynamicData->setValueOfKey(GOLD_KEY, gold);
	//更新显示
	m_pFarmUILayer->updateShowingGold(gold.asInt());
	m_pGoodLayer->updateShowingGold(gold.asInt());

	//创建一个Soil
	auto soil = m_pSoilLayer->addSoil(soilID, 1);
	//更新存档
	dynamicData->updateSoil(soil);
	//土地全部购买 隐藏扩展土地精灵
	if (soilID == 0)
	{
		m_pBrandSprite->setVisible(false);
	}
	else
	{
		soilID--;
		Value value = Value(soilID);
		dynamicData->setValueOfKey(FARM_EXTENSIBLE_SOIL_KEY, value);
		auto pos = m_pSoilLayer->getSoilPositionByID(soilID);
		m_pBrandSprite->setPosition(pos);
	}
}

bool FarmScene::preloadResources()
{
	//加载资源
        auto spriteCache = Director::getInstance()->getSpriteFrameCache();

        spriteCache->addSpriteFramesWithFile("sprite/farm_crop_res.xml");
        spriteCache->addSpriteFramesWithFile("sprite/farm_ui_res.xml");
        spriteCache->addSpriteFramesWithFile("sprite/good_layer_ui_res.xml");
        spriteCache->addSpriteFramesWithFile("sprite/slider_dialog_ui_res.xml");
        spriteCache->addSpriteFramesWithFile("sprite/butterfly.xml");

        return true;

}

void FarmScene::initializeSoilsAndCrops()
{
	//读取存档
	auto& farmValueVec = DynamicData::getInstance()->getValueOfKey("soils")->asValueVector();

	for (auto& value : farmValueVec)
	{
		int soilID = 0;
		int soilLv = 0;
		int cropID = 0;
		int startTime = 0;
		int harvestCount = 1;
		float rate = 0.f;
		auto& valueMap = value.asValueMap();

		for (auto it = valueMap.begin(); it != valueMap.end(); it++)
		{
			auto& name = it->first;
			auto& value = it->second;

			if (name == "soil_id")
				soilID = value.asInt();
			else if (name == "soil_lv")
				soilLv = value.asInt();
			else if (name == "crop_id")
				cropID = value.asInt();
			else if (name == "crop_start")
				startTime = value.asInt();
			else if (name == "harvest_count")
				harvestCount = value.asInt();
			else if (name == "crop_rate")
				rate = value.asFloat();
		}
		//生成土壤对象
		Soil* soil = m_pSoilLayer->addSoil(soilID, soilLv);
		//是否存在对应的作物ID
		CropStruct* pCropSt = StaticData::getInstance()->getCropStructByID(cropID);
		if (pCropSt == nullptr)
			continue;

		Crop* crop = m_pCropLayer->addCrop(cropID, startTime, harvestCount, rate);
		crop->setSoil(soil);
		soil->setCrop(crop);
		//设置位置
		crop->setPosition(soil->getPosition());
	}
}

void FarmScene::initializeShopGoods()
{
	//初始化生成商店背包列表
	string seed_shop_list = DynamicData::getInstance()->getValueOfKey("seed_shop_list")->asString();
	auto callback = [this](int, Value value)
	{
		Seed* seed = Seed::create(value.asInt(), 10);
		SDL_SAFE_RETAIN(seed);
		m_shopList.push_back(seed);
	};
	StringUtils::split(seed_shop_list, ",", callback);
}

void FarmScene::setVisibleofGoodLayer(bool visible)
{
	//动画tag
	const int tag = 1;
	//动作显示
	Size visibleSize = Director::getInstance()->getVisibleSize();
	ActionInterval* action = nullptr;
	//出现
	if (visible)
	{
		MoveTo* move = MoveTo::create(0.5f,Point(0, 0));
		action = EaseExponentialOut::create(move);
	}
	else
	{
		MoveTo* move = MoveTo::create(0.5f,Point(0, -visibleSize.height));
		action = EaseExponentialIn::create(move);
	}
	action->setTag(tag);
	
	m_pGoodLayer->setShowing(visible);
	//停止原先动画并开始新动画
	m_pGoodLayer->stopActionByTag(tag);
	m_pGoodLayer->runAction(action);
}

void FarmScene::showGoodLayer(const string& titleFrameName, const string& btnFrameName
		, const vector<Good*>* vec, int curPage)
{
	this->setVisibleofGoodLayer(true);
	//设置title
	m_pGoodLayer->updateShowingTitle(titleFrameName);
	//设置使用按钮
	m_pGoodLayer->updateShowingBtn(BtnType::Use, BtnParamSt(true, true, btnFrameName));
	//隐藏装备按钮
	m_pGoodLayer->updateShowingBtn(BtnType::Equip, BtnParamSt(false, false));
	//更新页码
	int size = vec->size();
	auto totalPage = size / 4;
	if (size % 4 != 0)
		totalPage += 1;

	if (totalPage == 0)
		totalPage = 1;

	//保证页面合法
	m_nCurPage = curPage;
	if (m_nCurPage > totalPage)
		m_nCurPage--;
	if (m_nCurPage <= 0)
		m_nCurPage = 1;
	
	//切片处理
	vector<GoodInterface*> content;
	for (int i = 0; i < 4; i++)
	{
		int index = (m_nCurPage - 1) * 4 + i;
		
		if (index >= size)
			break;
		content.push_back(vec->at(index));
	}
	m_pGoodLayer->updateShowingPage(m_nCurPage, totalPage);
	//填充物品
	m_pGoodLayer->updateShowingGoods(content);
}

Value FarmScene::getValueOfKey(const string& key)
{
	auto dynamicData = DynamicData::getInstance();
	Value* p = dynamicData->getValueOfKey(key);
	Value value;
	//不存在对应的键，自行设置
	if (p == nullptr)
	{
		//农场默认等级
		if (key == FARM_LEVEL_KEY)
			value = Value(1);
		//农场默认经验
		else if (key == FARM_EXP_KEY)
			value = Value(0);
		//默认金币
		else if (key == GOLD_KEY)
			value = Value(0);
		else if (key == FARM_EXTENSIBLE_SOIL_KEY)
			value = Value(11);
		//设置值
		dynamicData->setValueOfKey(key, value);
	}
	else
	{
		value = *p;
	}
	return value;
}
