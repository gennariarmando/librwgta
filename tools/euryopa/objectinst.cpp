#include "euryopa.h"

CPtrList instances;
CPtrList selection;

void
ObjectInst::UpdateMatrix(void)
{
	m_matrix.rotate(conj(m_rotation), rw::COMBINEREPLACE);
	m_matrix.scale(&m_scale, rw::COMBINEPOSTCONCAT);
	m_matrix.translate(&m_translation, rw::COMBINEPOSTCONCAT);
}

void*
ObjectInst::CreateRwObject(void)
{
	rw::Frame *f;
	rw::Atomic *atomic;
	rw::Clump *clump;
	ObjectDef *obj = GetObjectDef(m_objectId);

	if(obj->m_type == ObjectDef::ATOMIC){
		if(obj->m_atomics[0] == nil)
			return nil;
		atomic = obj->m_atomics[0]->clone();
		f = rw::Frame::create();
		atomic->setFrame(f);
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = atomic;
	}else if(obj->m_type == ObjectDef::CLUMP){
		if(obj->m_clump == nil)
			return nil;
		clump = obj->m_clump->clone();
		f = clump->getFrame();
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = clump;
	}
	return m_rwObject;
}

void
ObjectInst::Init(FileObjectInstance *fi)
{
	m_altered = false;
	m_objectId = fi->objectId;
	if(fi->area & 0x100) m_isUnimportant = true;
	if(fi->area & 0x400) m_isUnderWater = true;
	if(fi->area & 0x800) m_isTunnel = true;
	if(fi->area & 0x1000) m_isTunnelTransition = true;
	m_area = fi->area & 0xFF;
	m_rotation = fi->rotation;
	m_prevRotation = fi->rotation;

	m_translation = fi->position;
	m_prevTranslation = fi->position;

	m_scale = fi->scale;
	m_prevScale = fi->scale;

	m_lodId = fi->lod;
	UpdateMatrix();
}

void
ObjectInst::SetupBigBuilding(void)
{
	m_isBigBuilding = true;
}

CRect
ObjectInst::GetBoundRect(void)
{
	CRect rect;
	rw::V3d v;
	CColModel *col = GetObjectDef(m_objectId)->m_colModel;

	v = col->boundingBox.min;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.max;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.min;
	v.x = col->boundingBox.max.x;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.max;
	v.x = col->boundingBox.min.x;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	return rect;
}

bool
ObjectInst::IsOnScreen(void)
{
	rw::Sphere sph;
	ObjectDef *obj = GetObjectDef(m_objectId);
	CColModel *col = obj->m_colModel;
	sph.center = col->boundingSphere.center;
	sph.radius = col->boundingSphere.radius;
	return TheCamera.IsSphereVisible(&sph, &m_matrix);
}

static void
updateMatFXAnim(rw::Material *m)
{
	if(!rw::UVAnim::exists(m))
		return;
	rw::UVAnim::addTime(m, timeStep);
	rw::UVAnim::applyUpdate(m);
}

void
ObjectInst::PreRender(void)
{
	int i;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj->m_hasPreRendered)
		return;
	obj->m_hasPreRendered = true;

	if(obj->m_type == ObjectDef::ATOMIC && gPlayAnimations){
		rw::Atomic *atm = (rw::Atomic*)m_rwObject;
		if(rw::MatFX::getEffects(atm))
			for(i = 0; i < atm->geometry->matList.numMaterials; i++)
				updateMatFXAnim(atm->geometry->matList.materials[i]);
	}
}

void
ObjectInst::JumpTo(void)
{
	rw::V3d center;
	ObjectDef *obj = GetObjectDef(m_objectId);
	CSphere *sph = &obj->m_colModel->boundingSphere;
	rw::V3d::transformPoints(&center, &sph->center, 1, &m_matrix);
	TheCamera.setTarget(center);
	TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(sph->radius));
}

void
ObjectInst::Select(void)
{
	if(m_selected) return;
	m_selected = true;
	selection.InsertItem(this);
}

void
ObjectInst::Deselect(void)
{
	CPtrNode *p;
	if(!m_selected) return;
	m_selected = false;
	for(p = selection.first; p; p = p->next)
		if(p->item == this){
			selection.RemoveNode(p);
			return;
		}
}

void
ClearSelection(void)
{
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		((ObjectInst*)p->item)->m_selected = false;
		selection.RemoveNode(p);
	}
}


ObjectInst*
GetInstanceByID(int32 id)
{
	CPtrNode *p;
	for(p = instances.first; p; p = p->next)
		if(((ObjectInst*)p->item)->m_id == id)
			return (ObjectInst*)p->item;
	return nil;
}

ObjectInst*
AddInstance(void)
{
	static int32 id = 1;

	ObjectInst *inst = new ObjectInst;
	memset(inst, 0, sizeof(ObjectInst));
	inst->m_id = id++;
	instances.InsertItem(inst);
	return inst;
}







int numSectorsX, numSectorsY;
CRect worldBounds;
Sector *sectors;
Sector outOfBoundsSector;

void
InitSectors(void)
{
	switch(params.map){
	case GAME_III:
		numSectorsX = 100;
		numSectorsY = 100;
		worldBounds.left = -2000.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 2000.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_VC:
		numSectorsX = 80;
		numSectorsY = 80;
		worldBounds.left = -2400.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 1600.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_SA:
		numSectorsX = 120;
		numSectorsY = 120;
		worldBounds.left = -3000.0f;
		worldBounds.bottom = -3000.0f;
		worldBounds.right = 3000.0f;
		worldBounds.top = 3000.0f;
		break;
	}
	sectors = new Sector[numSectorsX*numSectorsY];
}

Sector*
GetSector(int ix, int iy)
{
	return &sectors[ix*numSectorsY + iy];
}

//Sector*
//GetSector(float x, float y)
//{
//	int i, j;
//	assert(x > worldBounds.left && x < worldBounds.right);
//	assert(y > worldBounds.bottom && y < worldBounds.top);
//	i = (x + worldBounds.right - worldBounds.left)/numSectorsX;
//	j = (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
//	return GetSector(i, j);
//}

int
GetSectorIndexX(float x)
{
	assert(x >= worldBounds.left && x < worldBounds.right);
	return (x + worldBounds.right - worldBounds.left)/numSectorsX;
}

int
GetSectorIndexY(float y)
{
	assert(y >= worldBounds.bottom && y < worldBounds.top);
	return (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
}

bool
IsInstInBounds(ObjectInst *inst)
{
	ObjectDef *obj;
	// Some objects don't have collision data
	// TODO: figure out what to do with them
	obj = GetObjectDef(inst->m_objectId);
	if(obj->m_colModel == nil)
		return false;
	CRect bounds = inst->GetBoundRect();
	return bounds.left >= worldBounds.left &&
		bounds.right < worldBounds.right &&
		bounds.bottom >= worldBounds.bottom &&
		bounds.top < worldBounds.top;
}

void
InsertInstIntoSectors(ObjectInst *inst)
{
	Sector *s;
	CPtrList *list;
	int x, xstart, xmid, xend;
	int y, ystart, ymid, yend;

	if(!IsInstInBounds(inst)){
		list = inst->m_isBigBuilding ? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
		return;
	}

	CRect bounds = inst->GetBoundRect();

	xstart = GetSectorIndexX(bounds.left);
	xend   = GetSectorIndexX(bounds.right);
	xmid   = GetSectorIndexX((bounds.left + bounds.right)/2.0f);
	ystart = GetSectorIndexY(bounds.bottom);
	yend   = GetSectorIndexY(bounds.top);
	ymid   = GetSectorIndexY((bounds.bottom + bounds.top)/2.0f);

	for(y = ystart; y <= yend; y++)
		for(x = xstart; x <= xend; x++){
			s = GetSector(x, y);
			if(x == xmid && y == ymid)
				list = inst->m_isBigBuilding ? &s->bigbuildings : &s->buildings;
			else
				list = inst->m_isBigBuilding ? &s->bigbuildings_overlap : &s->buildings_overlap;
			list->InsertItem(inst);
		}
}

