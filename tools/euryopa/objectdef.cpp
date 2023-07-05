#include "euryopa.h"

static ObjectDef *objdefs[NUMOBJECTDEFS];

float
ObjectDef::GetLargestDrawDist(void)
{
	int i;
	float dd = 0.0f;
	for(i = 0; i < this->m_numAtomics; i++)
		if(this->m_drawDist[i] > dd)
			dd = this->m_drawDist[i];
	return dd;
}

rw::Atomic*
ObjectDef::GetAtomicForDist(float dist)
{
	// TODO: handle damaged atomics
	int i;
	for(i = 0; i < this->m_numAtomics; i++)
		if(dist < this->m_drawDist[i]*TheCamera.m_LODmult)
			return this->m_atomics[i];
	// We never want to return nil, so just pick one for the largest distance
	int n = 0;
	float dd = 0.0f;
	for(i = 0; i < this->m_numAtomics; i++)
		if(this->m_drawDist[i] > dd){
			dd = this->m_drawDist[i];
			n = i;
		}
	return this->m_atomics[n];
}

bool
ObjectDef::IsLoaded(void)
{
	if(m_type == ATOMIC)
		return m_atomics[0] != nil;
	else if(m_type == CLUMP)
		return m_clump != nil;
	return false;	// can't happen
}

static void
GetNameAndLOD(char *nodename, char *name, int *n)
{
	char *underscore = nil;
	for(char *s = nodename; *s != '\0'; s++){
		if(s[0] == '_' && (s[1] == 'l' || s[1] == 'L'))
			underscore = s;
	}
	if(underscore){
		strncpy(name, nodename, underscore - nodename);
		name[underscore - nodename] = '\0';
		*n = atoi(underscore + 2);
	}else{
		strncpy(name, nodename, 24);
		*n = 0;
	}
}

static void
GetNameAndDamage(char *nodename, char *name, int *n)
{
	int len;
	len = strlen(nodename);
	if(strcmp(&nodename[len-4], "_dam") == 0){
		*n = 1;
		strncpy(name, nodename, len-4);
		name[len-4] = '\0';
	}else if(strcmp(&nodename[len-3], "_l0") == 0 ||
	         strcmp(&nodename[len-3], "_L0") == 0){
		*n = 0;
		strncpy(name, nodename, len-3);
		name[len-3] = '\0';
	}else{
		*n = 0;
		strncpy(name, nodename, len);
		name[len] = '\0';
	}
}

static void
SetupAtomic(rw::Atomic *atm)
{
	// Make sure we are not pre-instanced
	gta::attachCustomPipelines(atm);	// attach xbox pipelines, which we want to uninstance
	int32 driver = rw::platform;
	int32 platform = rw::findPlatform(atm);
	if(platform){
		rw::platform = platform;
		rw::switchPipes(atm, rw::platform);
	}
	if(atm->geometry->flags & rw::Geometry::NATIVE)
		atm->uninstance();
	rw::ps2::unconvertADC(atm->geometry);
	rw::platform = driver;
	// no need to switch back pipes because we reset it anyway

//	rw::MatFX::disableEffects(atm);	// so cloning won't reattach any MatFX pipes

	if(params.neoWorldPipe)
		atm->pipeline = neoWorldPipe;
	else if(params.leedsPipe)
		atm->pipeline = gta::leedsPipe;
	else if(params.daynightPipe && IsBuildingPipeAttached(atm))
		SetupBuildingPipe(atm);
	else{
		if(params.daynightPipe)
			// TEMPORARY because our MatFX can't do UV anim yet
			SetupBuildingPipe(atm);
		else
			atm->pipeline = nil;
	}
	atm->setRenderCB(myRenderCB);
}

void
ObjectDef::CantLoad(void)
{
	log("Can't load object %s\n", m_name);
	m_cantLoad = true;
}

static rw::Clump*
loadclump(rw::Stream *stream)
{
	using namespace rw;
	rw::Clump *c = nil;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(stream, &header);
	UVAnimDictionary *prev = currentUVAnimDictionary;
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(stream);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(stream, &header);
	}
	if(header.type == ID_CLUMP)
		c = Clump::streamRead(stream);
	currentUVAnimDictionary= prev;
	return c;
}

void
ObjectDef::LoadAtomic(void)
{
	uint8 *buffer;
	int size;
	rw::StreamMemory stream;
	rw::Clump *clump;
	rw::Atomic *atomic;
	char *nodename;
	char name[MODELNAMELEN];
	int n;

	buffer = ReadFileFromImage(this->m_imageIndex, &size);
	stream.open((uint8*)buffer, size);
	clump = loadclump(&stream);
	if(clump){
		FORLIST(lnk, clump->atomics){
			atomic = rw::Atomic::fromClump(lnk);
			nodename = gta::getNodeName(atomic->getFrame());
			if(!isSA())
				GetNameAndLOD(nodename, name, &n);
			else
				GetNameAndDamage(nodename, name, &n);
			SetAtomic(n, atomic);
			atomic->clump->removeAtomic(atomic);
			atomic->setFrame(rw::Frame::create());
			SetupAtomic(atomic);
		}
		clump->destroy();
		if(m_atomics[0] == nil)
			CantLoad();
	}
	stream.close();
}

void
ObjectDef::LoadClump(void)
{
	uint8 *buffer;
	int size;
	rw::StreamMemory stream;
	rw::Clump *clump;
	rw::Atomic *atomic;

	buffer = ReadFileFromImage(this->m_imageIndex, &size);
	stream.open((uint8*)buffer, size);
	clump = loadclump(&stream);
	if(clump){
		FORLIST(lnk, clump->atomics){
			atomic = rw::Atomic::fromClump(lnk);
			SetupAtomic(atomic);
		}
		SetClump(clump);
		if(m_clump == nil)
			CantLoad();
	}
	stream.close();
}

void
ObjectDef::Load(void)
{
	if(m_cantLoad)
		return;

	if(this->m_imageIndex < 0){
		log("warning: no streaming info for object %s\n", this->m_name);
		CantLoad();
		return;
	}
	if(this->m_txdSlot >= 0 && !IsTxdLoaded(this->m_txdSlot))
		LoadTxd(this->m_txdSlot);
	TxdMakeCurrent(this->m_txdSlot);

	if(m_type == ATOMIC)
		LoadAtomic();
	else if(m_type == CLUMP)
		LoadClump();
}

void
ObjectDef::SetAtomic(int n, rw::Atomic *atomic)
{
	if(this->m_atomics[n])
		log("warning: object %s already has atomic %d\n", m_name, n);
	this->m_atomics[n] = atomic;
	// TODO: set lighting
}

void
ObjectDef::SetClump(rw::Clump *clump)
{
	if(this->m_clump)
		log("warning: object %s already has clump\n", m_name);
	this->m_clump = clump;
	// TODO: set lighting
}

static ObjectDef*
FindRelatedObject(ObjectDef *obj, int first, int last)
{
	ObjectDef *obj2;
	int i;
	for(i = first; i < last; i++){
		obj2 = objdefs[i];
		if(obj2 && obj2 != obj &&
		   rw::strncmp_ci(obj->m_name+3, obj2->m_name+3, MODELNAMELEN) == 0)
			return obj2;
	}
	return nil;
}

void
ObjectDef::SetupBigBuilding(int first, int last)
{
	ObjectDef *hqobj;
	if(m_drawDist[0] > LODDISTANCE)
		m_isBigBuilding = true;

	// in SA level of detail is handled by instances
	if(!isSA() && m_isBigBuilding && m_relatedModel == nil){
		hqobj = FindRelatedObject(this, first, last);
		if(hqobj){
			hqobj->m_relatedModel = this;
			m_relatedModel = hqobj;
			m_minDrawDist = hqobj->GetLargestDrawDist();
		}else
			m_minDrawDist = params.map == GAME_III ? 100.0f : 0.0f;
	}
}

void
ObjectDef::SetFlags(int flags)
{
	switch(params.objFlagset){
	case GAME_III:
		if(flags & 1) m_normalCull = true;
		if(flags & 2) m_noFade = true;
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x10) m_isSubway = true;
		if(flags & 0x20) m_ignoreLight = true;
		if(flags & 0x40) m_noZwrite = true;
		break;
	case GAME_VC:
		if(flags & 1) m_wetRoadReflection = true;
		if(flags & 2) m_noFade = true;
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x10) m_isSubway = true;	// probably used
		if(flags & 0x20) m_ignoreLight = true;
		if(flags & 0x40) m_noZwrite = true;
		if(flags & 0x80) m_noShadows = true;
		if(flags & 0x100) m_ignoreDrawDist = true;
		if(flags & 0x200) m_isCodeGlass = true;
		if(flags & 0x400) m_isArtistGlass = true;
		break;
	case GAME_SA:
		// base model info
		if(flags & (4|8)) m_drawLast = true;
		if(flags & 8) m_additive = true;
		if(flags & 0x40) m_noZwrite = true;
		if(flags & 0x80) m_noShadows = true;
		if(flags & 0x200000) m_noBackfaceCulling = true;

		if(m_type == ATOMIC){
			if(flags & 1) m_wetRoadReflection = true;
			if(flags & 0x8000) m_dontCollideWithFlyer = true;
			// these are exclusive:
			if(flags &    0x200) m_isCodeGlass = true;
			if(flags &    0x400) m_isArtistGlass = true;
			if(flags &    0x800) m_isGarageDoor = true;
			if(!m_isTimed)
				if(flags & 0x1000) m_isDamageable = true;
			if(flags &   0x2000) m_isTree = true;
			if(flags &   0x4000) m_isPalmTree = true;
			if(flags & 0x100000) m_isTag = true;
			if(flags & 0x400000) m_noCover = true;
			if(flags & 0x800000) m_wetOnly = true;

//			if(flags & ~0xF0FECD)
//				printf("Object has unknown flags: %s %x %x\n", m_name, flags, flags&~0xF0FECD);
		}else if(m_type == CLUMP){
			if(flags & 0x20) m_isDoor = true;
		}
		break;
	}
}

ObjectDef*
AddObjectDef(int id)
{
	ObjectDef *obj = new ObjectDef;
	memset(obj, 0, sizeof(ObjectDef));
	obj->m_imageIndex = -1;
	// TODO: warn if already defined
	objdefs[id] = obj;
	obj->m_id = id;
	return obj;
}

ObjectDef*
GetObjectDef(int id)
{
	return objdefs[id];
}

ObjectDef*
GetObjectDef(const char *name, int *id)
{
	int i;
	for(i = 0; i < nelem(objdefs); i++){
		if(objdefs[i] == nil)
			continue;
		if(rw::strncmp_ci(objdefs[i]->m_name, name, MODELNAMELEN) == 0){
			if(id)
				*id = i;
			return objdefs[i];
		}
	}
	return nil;
}

ObjectDef**
GetObjectDef()
{
	return objdefs;
}
