#define WITH_D3D
#include "euryopa.h"
#include "templates.h"

struct InstDist
{
	ObjectInst *inst;
	union { float dist, sort; };
};

static ObjectInst *visibleInsts[60000];
static int numVisibleInsts;

static CLinkList<InstDist> sortedInstList;

static InstDist lodList[100];
static int numLods;

static uint16 currentScanCode;

static rw::ObjPipeline *colourCodePipe;

enum Visibility
{
	VIS_INVISIBLE,
	VIS_VISIBLE,
	VIS_CULLED,
	VIS_STREAMME
};

static void
AddToRenderList(ObjectInst *inst, float dist)
{
	if (inst->removed)
		return;

	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	assert(inst->m_rwObject);

	inst->PreRender();
	if(obj->m_drawLast){
		InstDist e;
		e.inst = inst;
		e.dist = dist;
		sortedInstList.InsertSorted(e);
	}else
		visibleInsts[numVisibleInsts++] = inst;
}

// This is a list of potentially rendered LODs
static void
AddToLodRenderList(ObjectInst *inst, float dist)
{
	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		int i = numLods++;
		lodList[i].inst = inst;
		lodList[i].dist = dist;
	}else
		AddToRenderList(inst, dist);
}

// Don't assume there are LODs, just draw if below draw distance
static int
SetupVisibilitySimple(ObjectInst *inst, float *distout)
{
	ObjectDef *obj, *hdobj;
	float camdist;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);

	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		hdobj = inst->m_isBigBuilding ? obj->m_relatedModel : nil;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		camdist = TheCamera.distanceTo(inst->m_translation);
		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	if(!inst->IsOnScreen())
		return VIS_CULLED;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			instatm->setGeometry(atm->geometry, 0);
	}

	return VIS_VISIBLE;
}

static int
SetupVisibilityIII(ObjectInst *inst, float *distout)
{
	ObjectDef *obj, *hdobj;
	float camdist;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);

	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		hdobj = inst->m_isBigBuilding ? obj->m_relatedModel : nil;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		camdist = TheCamera.distanceTo(inst->m_translation);
		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;
		if(camdist < obj->m_minDrawDist*TheCamera.m_LODmult)
			if(hdobj == nil || hdobj->IsLoaded())
				return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	if(!inst->IsOnScreen())
		return VIS_CULLED;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			instatm->setGeometry(atm->geometry, 0);
	}

	return VIS_VISIBLE;
}

static int
SetupVisibilitySA(ObjectInst *inst, float camdist)
{
	ObjectDef *obj;
	ObjectInst *lodinst;
	rw::Atomic *atm, *instatm;
	rw::Clump *instclump;

	obj = GetObjectDef(inst->m_objectId);
	lodinst = inst->m_lod;

	// Highlighted objects are always visible
	if(!inst->m_selected && inst->m_highlight == 0){
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			return VIS_INVISIBLE;

		if(obj->m_isHidden)
			return VIS_INVISIBLE;

		if(camdist >= obj->GetLargestDrawDist()*TheCamera.m_LODmult)
			return VIS_INVISIBLE;

		if(!gNoTimeCull && obj->m_isTimed && !IsHourInRange(obj->m_timeOn, obj->m_timeOff))
			return VIS_INVISIBLE;
	}else
		camdist = TheCamera.distanceTo(inst->m_translation);

	if(!obj->IsLoaded())
		return VIS_STREAMME;

	instatm = nil;
	instclump = nil;
	if(inst->m_rwObject == nil)
		if(inst->CreateRwObject() == nil)
			return VIS_INVISIBLE;	// this shouldn't happen

	if(obj->m_type == ObjectDef::ATOMIC){
		atm = obj->GetAtomicForDist(camdist);
		if(atm == nil)	// this shouldn't happen but it does (in xbox III)
			return VIS_INVISIBLE;
		instatm = (rw::Atomic*)inst->m_rwObject;
		if(instatm->geometry != atm->geometry)
			instatm->setGeometry(atm->geometry, 0);
	}

	int ret = VIS_VISIBLE;
	if(!inst->IsOnScreen())
		ret = VIS_CULLED;	// we want to render but we're culled

	if(lodinst == nil)
		return ret;

	lodinst->m_numChildrenRendered++;
	if(lodinst->m_numChildren > 1){
		AddToLodRenderList(inst, camdist);
		return VIS_INVISIBLE;
	}
	return ret;
}

static int
SetupBigBuildingVisibilitySA(ObjectInst *inst, float *distout)
{
	int v;
	ObjectInst *lodinst;
	float camdist;

	lodinst = inst->m_lod;

	camdist = TheCamera.distanceTo(inst->m_translation);
	*distout = camdist;
	if(inst->m_numChildrenRendered > 0){
		// There is something to render here...

		// tell LOD parent that we (or children) want to be rendered
		if(lodinst)
			lodinst->m_numChildrenRendered++;

		if(inst->m_numChildren > 1)
			// might still have to render this LOD,
			// if not all children are rendered
			// We actually don't test for frustum culling here...TODO?
			AddToLodRenderList(inst, camdist);
		else
			// otherwise...???
			inst->m_numChildrenRendered = 0;
		return VIS_INVISIBLE;
	}else{
		// No child wants to be rendered (yet?)
		// but maybe this LOD...

		v = SetupVisibilitySA(inst, camdist);

		if(v == VIS_VISIBLE && inst->m_numChildren > 1){
			// we're visible AND have multiple children,
			// so we might have to render
			AddToLodRenderList(inst, camdist);
			return VIS_INVISIBLE;
		}
		// But what if we're a super LOD and our child
		// just hasn't been visited yet?
		// Maybe that isn't allowed?
		// With v == 1 and m_numChildren == 1 how is this correct?
		return v;
	}
}

static int
SetupHdVisibilitySA(ObjectInst *inst, float *distout)
{
	float camdist = TheCamera.distanceTo(inst->m_translation);
	*distout = camdist;
	return SetupVisibilitySA(inst, camdist);
}

static void
ProcessLodList(void)
{
	int i;
	ObjectInst *inst;//, *superlod;

	// I don't understand SA's code of this function...
	// So this one doesn't make perfect sense either.

	for(i = 0; i < numLods; i++){
		inst = lodList[i].inst;
		if(inst == nil) continue;
		if(inst->m_numChildren > 0 && inst->m_numChildrenRendered == inst->m_numChildren){
			// all children can be drawn, so don't draw the LOD
			inst->m_numChildrenRendered = 0;
			lodList[i].inst = nil;
		}else{
			// not all children are drawn

			// We don't have the RW object yet, in case we didn't call the visbility function
			if(inst->m_rwObject == nil)
				inst->CreateRwObject();

			//superlod = inst->m_lod;
			//if(superlod){
			//}
		}
//		printf("%d %s %d %d\n", i, GetObjectDef(inst->m_objectId)->m_name,
//			inst->m_numChildren, inst->m_numChildrenRendered);
	}

	// I don't get this, do we really want to hide superlods with this?
//	for(i = 0; i < numLods; i++){
//		inst = lodList[i];
//		if(inst == nil) continue;
//		// what's this exactly?
//		if(inst->m_numChildrenRendered > 0){
//			inst->m_numChildrenRendered = 0;
//			lodList[i] = 0;
//		}
//	}

	for(i = 0; i < numLods; i++){
		inst = lodList[i].inst;
		if(inst == nil) continue;
		AddToRenderList(inst, lodList[i].dist);
	}
}

static rw::RGBA highlightColor;

void
myRenderCB(rw::Atomic *atomic)
{
	if(gta::renderColourCoded)
		colourCodePipe->render(atomic);
	else if(highlightColor.red || highlightColor.green || highlightColor.blue){
		atomic->getPipeline()->render(atomic);
		gta::colourCode = highlightColor;
		gta::colourCode.alpha = 128;
		int32 zwrite, fog, aref;
		zwrite = GetRenderState(rw::ZWRITEENABLE);
		fog = rw::GetRenderState(rw::FOGENABLE);
		aref = rw::GetRenderState(rw::ALPHATESTREF);
		SetRenderState(rw::ZWRITEENABLE, 0);
		SetRenderState(rw::FOGENABLE, 0);
		SetRenderState(rw::ALPHATESTREF, 10);
		colourCodePipe->render(atomic);
		SetRenderState(rw::ZWRITEENABLE, zwrite);
		SetRenderState(rw::FOGENABLE, fog);
		SetRenderState(rw::ALPHATESTREF, aref);
	}else
		atomic->getPipeline()->render(atomic);
}

static void
RenderInst(ObjectInst *inst)
{
	static rw::RGBA black = { 0, 0, 0, 255 };
	static rw::RGBA red = { 255, 0, 0, 255 };
	static rw::RGBA green = { 0, 255, 0, 255 };
	static rw::RGBA blue = { 0, 0, 255, 255 };
	static rw::RGBA highlightCols[] = { black, green, red, blue };
	ObjectDef *obj;

//	if(!inst->m_isUnimportant && !inst->m_isUnderWater && !inst->m_isTunnel && !inst->m_isTunnelTransition)
//		return;

	obj = GetObjectDef(inst->m_objectId);
	obj->m_hasPreRendered = false;

	uint32 cull;
	if(obj->m_noBackfaceCulling){
		cull = GetRenderState(rw::CULLMODE);
		SetRenderState(rw::CULLMODE, rw::CULLNONE);
	}

	pDirect->setFlags(0);
	gta::colourCode.red = inst->m_id & 0xFF;
	gta::colourCode.green = inst->m_id>>8 & 0xFF;
	gta::colourCode.blue = inst->m_id>>16 & 0xFF;
	gta::colourCode.alpha = 255;

	if(inst->m_selected && inst->m_highlight < HIGHLIGHT_SELECTION)
		inst->m_highlight = HIGHLIGHT_SELECTION;
	highlightColor = highlightCols[inst->m_highlight];

	if (obj->m_type == ObjectDef::ATOMIC) {
		((rw::Atomic*)inst->m_rwObject)->getFrame()->matrix = inst->m_matrix;

		((rw::Atomic*)inst->m_rwObject)->getFrame()->updateObjects();
		((rw::Atomic*)inst->m_rwObject)->render();
	}
	else if (obj->m_type == ObjectDef::CLUMP) {
		((rw::Clump*)inst->m_rwObject)->getFrame()->matrix = inst->m_matrix;

		((rw::Clump*)inst->m_rwObject)->getFrame()->updateObjects();
		((rw::Clump*)inst->m_rwObject)->render();
	}
	highlightColor = black;
	if(obj->m_noBackfaceCulling)
		SetRenderState(rw::CULLMODE, cull);
	pDirect->setFlags(rw::Light::LIGHTATOMICS);
}

static void
RenderTransparentInst(ObjectInst *inst)
{
	ObjectDef *obj;
	obj = GetObjectDef(inst->m_objectId);

	if(obj->m_noZwrite){
		SetRenderState(rw::ZWRITEENABLE, 0);
		SetRenderState(rw::ALPHATESTREF, 0);
	}else
		SetRenderState(rw::ALPHATESTREF, params.alphaRef);

//	This is not handled that way by GTA, only on fading entities....
//	if(obj->m_additive)
//		SetRenderState(rw::DESTBLEND, rw::BLENDONE);

	RenderInst(inst);

	if(obj->m_noZwrite)
		SetRenderState(rw::ZWRITEENABLE, 1);
//	SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

static void
ProcessBuilding(ObjectInst *inst)
{
	float dist;
	if(inst->m_isBigBuilding || inst->m_scanCode == currentScanCode)
		return;
	inst->m_scanCode = currentScanCode;
	int v;
	if(gRenderOnlyHD)
		v = SetupVisibilitySimple(inst, &dist);
	else
		v = isSA() ? 
			SetupHdVisibilitySA(inst, &dist) :
			SetupVisibilityIII(inst, &dist);
	if(v == VIS_VISIBLE)
		AddToRenderList(inst, dist);
	else if(v == VIS_STREAMME)
		RequestObject(inst->m_objectId);
}

static void
ProcessBigBuilding(ObjectInst *inst)
{
	float dist;
	if(!inst->m_isBigBuilding || inst->m_scanCode == currentScanCode)
		return;
	inst->m_scanCode = currentScanCode;
	int v;
	if(gRenderOnlyLod)
		v = SetupVisibilitySimple(inst, &dist);
	else
		v = isSA() ? 
			SetupBigBuildingVisibilitySA(inst, &dist) :
			SetupVisibilityIII(inst, &dist);
	if(v == VIS_VISIBLE)
		AddToRenderList(inst, dist);
	else if(v == VIS_STREAMME)
		RequestObject(inst->m_objectId);
}

static void
ScanInstList(CPtrList *list, void (*f)(ObjectInst *inst))
{
	CPtrNode *p;
	for(p = list->first; p; p = p->next)
		f((ObjectInst*)p->item);
}

void
BuildRenderList(void)
{
	CPtrNode *p;
	ObjectInst *inst;

	static bool listInit;
	if(!listInit){
		sortedInstList.Init(2000);
		listInit = true;
	}

	numVisibleInsts = 0;
	numLods = 0;
	sortedInstList.Clear();

	currentScanCode++;

	rw::BBox frustBox = TheCamera.m_rwcam->frustumBoundBox;
//	printf("%f %f %f    %f %f %f\n",
//		frustBox.inf.x, frustBox.inf.y, frustBox.inf.z, 
//		frustBox.sup.x, frustBox.sup.y, frustBox.sup.z);
	// clip to world boundaries
	frustBox.inf.x = clamp(frustBox.inf.x, worldBounds.left, worldBounds.right-1.0f);
	frustBox.inf.y = clamp(frustBox.inf.y, worldBounds.bottom, worldBounds.top-1.0f);
	frustBox.sup.x = clamp(frustBox.sup.x, worldBounds.left, worldBounds.right-1.0f);
	frustBox.sup.y = clamp(frustBox.sup.y, worldBounds.bottom, worldBounds.top-1.0f);

	int x, xstart, xend;
	int y, ystart, yend;
	xstart = GetSectorIndexX(frustBox.inf.x);
	xend   = GetSectorIndexX(frustBox.sup.x);
	ystart = GetSectorIndexY(frustBox.inf.y);
	yend   = GetSectorIndexY(frustBox.sup.y);
//	log("x: %d - %d; y: %d - %d\n", xstart, xend, ystart, yend);

	if(!gRenderOnlyLod){
		for(x = xstart; x <= xend; x++)
			for(y = ystart; y <= yend; y++){
				Sector *s = GetSector(x, y);
				ScanInstList(&s->buildings, ProcessBuilding);
				ScanInstList(&s->buildings_overlap, ProcessBuilding);
			}
		ScanInstList(&outOfBoundsSector.buildings, ProcessBigBuilding);
	}
	if(!gRenderOnlyHD){
		for(x = xstart; x <= xend; x++)
			for(y = ystart; y <= yend; y++){
				Sector *s = GetSector(x, y);
				ScanInstList(&s->bigbuildings, ProcessBigBuilding);
				ScanInstList(&s->bigbuildings_overlap, ProcessBigBuilding);
			}
		ScanInstList(&outOfBoundsSector.bigbuildings, ProcessBigBuilding);
	}

//	ScanInstList(&instances, ProcessBuilding);
//	ScanInstList(&instances, ProcessBigBuilding);

	ProcessLodList();

	// Reset instances
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		inst->m_numChildrenRendered = 0;
		inst->m_highlight = HIGHLIGHT_NONE;
	}

//	log("%d visible instances\n", numVisibleInsts);
}

void
RenderOpaque(void)
{
	SetRenderState(rw::ALPHATESTREF, params.alphaRef);

	SetRenderState(rw::CULLMODE, gDoBackfaceCulling ? rw::CULLBACK : rw::CULLNONE);
	int i;
	for(i = 0; i < numVisibleInsts; i++)
		RenderInst(visibleInsts[i]);
}

void
RenderTransparent(void)
{
	CLink<InstDist> *node;
	SetRenderState(rw::CULLMODE, gDoBackfaceCulling ? rw::CULLBACK : rw::CULLNONE);
	for(node = sortedInstList.tail.prev;
	    node && node != &sortedInstList.head;
	    node = node->prev){
		ObjectInst *inst = node->item.inst;
		RenderTransparentInst(inst);
	}
}

void
RenderEverything(void)
{
	RenderOpaque();
	RenderTransparent();
}

static void
RenderCollision(ObjectInst *inst)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj->m_colModel)
		RenderColModelWire(obj->m_colModel, &inst->m_matrix, obj->m_isBigBuilding);
	else
		printf("object %s has no LOD\n", obj->m_name);
}

void
RenderEverythingCollisions(void)
{
	int i;
	for(i = 0; i < numVisibleInsts; i++)
		RenderCollision(visibleInsts[i]);

	CLink<InstDist> *node;
	for(node = sortedInstList.tail.prev;
	    node != &sortedInstList.head;
	    node = node->prev){
		ObjectInst *inst = node->item.inst;
		RenderCollision(inst);
	}
}

void
setupLeedsPipe(void)
{
	gta::leedsPipe_amb = Timecycle::currentColours.amb;
	gta::leedsPipe_emiss = Timecycle::currentColours.amb_bl;
	switch(gBuildingPipeSwitch){
	case PLATFORM_PS2:
		gta::leedsPipe_platformSwitch = 1;
		break;
	case PLATFORM_PSP:
		gta::leedsPipe_platformSwitch = 0;
		break;
	default:
		gta::leedsPipe_platformSwitch = 2;
		break;
	}
}

#ifdef RW_D3D9
static void
leedsRenderCB(rw::Atomic *atomic, rw::d3d9::InstanceDataHeader *header)
{
	setupLeedsPipe();
	gta::leedsRenderCB(atomic, header);
}

void
MakeLeedsPipe(void)
{
	gta::MakeLeedsPipe();
	((rw::d3d9::ObjPipeline*)gta::leedsPipe)->renderCB = leedsRenderCB;
}
#endif

#ifdef RW_GL3
static void
leedsRenderCB(rw::Atomic *atomic, rw::gl3::InstanceDataHeader *header)
{
	setupLeedsPipe();
	gta::leedsRenderCB(atomic, header);
}

void
MakeLeedsPipe(void)
{
	gta::MakeLeedsPipe();
	((rw::gl3::ObjPipeline*)gta::leedsPipe)->renderCB = leedsRenderCB;
}
#endif

void
RegisterPipes(void)
{
#ifdef RW_GL3
	rw::gl3::registerUniform("u_dayparam");
	rw::gl3::registerUniform("u_nightparam");
	rw::gl3::registerUniform("u_texmat");
	rw::gl3::registerUniform("u_envmat");
	rw::gl3::registerUniform("u_envXform");
	rw::gl3::registerUniform("u_shininess");
	rw::gl3::registerUniform("u_colorscale");

	rw::gl3::registerUniform("tex0");
	rw::gl3::registerUniform("tex1");
	rw::gl3::registerUniform("u_lm");

	rw::gl3::registerUniform("u_postfxCol1");
	rw::gl3::registerUniform("u_postfxCol2");
	rw::gl3::registerUniform("u_postfxParams");

	rw::gl3::registerUniform("u_amb");
	rw::gl3::registerUniform("u_emiss");
#endif
}

void
RenderInit(void)
{
	colourCodePipe = gta::makeColourCodePipeline();
	MakeCustomBuildingPipelines();
	MakeNeoWorldPipe();
	MakeLeedsPipe();
}
