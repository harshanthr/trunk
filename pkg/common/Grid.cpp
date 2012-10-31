#include "Grid.hpp"
#ifdef YADE_OPENGL
	#include<yade/lib/opengl/OpenGLWrapper.hpp>
#endif

//!##################	SHAPES   #####################

GridNode::~GridNode(){}
YADE_PLUGIN((GridNode));

GridConnection::~GridConnection(){}
YADE_PLUGIN((GridConnection));

GridNodeGeom6D::~GridNodeGeom6D(){}
YADE_PLUGIN((GridNodeGeom6D));

ScGridCoGeom::~ScGridCoGeom(){}
YADE_PLUGIN((ScGridCoGeom));

void GridNode::addConnection(shared_ptr<Body> GC){
	ConnList.push_back(GC);
}

Vector3r GridConnection::getSegment(){
	return node2->state->pos_get() - node1->state->pos_get();
}

float GridConnection::getLength(){
	return getSegment().norm();
}



//!##################	IGeom Functors   #####################

//!			O-O
bool Ig2_GridNode_GridNode_GridNodeGeom6D::go( const shared_ptr<Shape>& cm1, const shared_ptr<Shape>& cm2, const State& state1, const State& state2, const Vector3r& shift2, const bool& force, const shared_ptr<Interaction>& c)
{	
	//GridConnection* GC = static_cast<GridConnection*>(cm.get());
	bool isNew = !c->geom;
	if (Ig2_Sphere_Sphere_ScGeom::go(cm1,cm2,state1,state2,shift2,force,c)){//the 3 DOFS from ScGeom are updated here
 		if (isNew) {//generate a 6DOF interaction from the 3DOF one generated by Ig2_Sphere_Sphere_ScGeom
			shared_ptr<GridNodeGeom6D> sc (new GridNodeGeom6D());
			*(YADE_PTR_CAST<ScGeom>(sc)) = *(YADE_PTR_CAST<ScGeom>(c->geom));
			c->geom=sc;
		}
		if (updateRotations) YADE_PTR_CAST<GridNodeGeom6D>(c->geom)->precomputeRotations(state1,state2,isNew,creep);
		if(YADE_PTR_CAST<GridNodeGeom6D>(c->geom)->connectionBody){	//test this because the connectionBody may not have been yet initialized.
			YADE_PTR_CAST<GridNodeGeom6D>(c->geom)->connectionBody->state->pos=state1.pos;
		}
		return true;
	}
	else return false;
}

bool Ig2_GridNode_GridNode_GridNodeGeom6D::goReverse( const shared_ptr<Shape>& cm1, const shared_ptr<Shape>& cm2, const State& state1, const State& state2, const Vector3r& shift2, const bool& force, const shared_ptr<Interaction>& c)
{
	return go(cm1,cm2,state2,state1,-shift2,force,c);
}
YADE_PLUGIN((Ig2_GridNode_GridNode_GridNodeGeom6D));


//!			O/
bool Ig2_Sphere_GridConnection_ScGridCoGeom::go(	const shared_ptr<Shape>& cm1,
						const shared_ptr<Shape>& cm2,
						const State& state1, const State& state2, const Vector3r& shift2, const bool& force,
						const shared_ptr<Interaction>& c)
{	// Useful variables :
	const State*    sphereSt  = YADE_CAST<const State*>(&state1);
	//const State*    gridCoSt  = YADE_CAST<const State*>(&state2);
	Sphere*         sphere    = YADE_CAST<Sphere*>(cm1.get());
	GridConnection* gridCo    = YADE_CAST<GridConnection*>(cm2.get());
	//GridNode*       gridNo1   = YADE_CAST<GridNode*>(gridCo->node1->shape.get());
	//GridNode*       gridNo2   = YADE_CAST<GridNode*>(gridCo->node2->shape.get());
	State*          gridNo1St = YADE_CAST<State*>(gridCo->node1->state.get());
	State*          gridNo2St = YADE_CAST<State*>(gridCo->node2->state.get());
	bool isNew = !c->geom;
	shared_ptr<ScGridCoGeom> scm;
	if (!isNew) scm = YADE_PTR_CAST<ScGridCoGeom>(c->geom);
	Vector3r segt = gridCo->getSegment();
	float len = gridCo->getLength();
	Vector3r branch = sphereSt->pos - gridNo1St->pos;
	float relPos = branch.dot(segt)/(len*len);
	Vector3r fictiousPos=gridNo1St->pos+relPos*segt;
	Vector3r branchP = fictiousPos - sphereSt->pos;
 	float dist = branchP.norm();
	
	if(isNew){
		if(dist > (sphere->radius + gridCo->radius)) return false;
		else {scm=shared_ptr<ScGridCoGeom>(new ScGridCoGeom()); c->geom=scm;}
	}
	if(dist <= (sphere->radius + gridCo->radius)){
		scm->refR1=sphere->radius;	//FIXME don't know why I have to do that ...
		scm->refR2=gridCo->radius;
		scm->id3=gridCo->node1->getId();
 		scm->id4=gridCo->node2->getId();
		scm->relPos=relPos;
		Vector3r normal=branchP/dist;
		scm->penetrationDepth=sphere->radius+gridCo->radius-dist;
		scm->fictiousState.pos = gridNo1St->pos+segt*relPos;
		scm->fictiousState.vel = (1-relPos)*gridNo1St->vel + relPos*gridNo2St->vel;
		scm->fictiousState.angVel =
			((1-relPos)*gridNo1St->angVel + relPos*gridNo2St->angVel).dot(segt/len)*segt/len //twist part : interpolated
			+ segt.cross(gridNo2St->vel - gridNo1St->vel);// non-twist part : defined from nodes velocities
		scm->contactPoint = sphereSt->pos+normal*(sphere->radius-0.5*scm->penetrationDepth);
		scm->precompute(state1,scm->fictiousState,scene,c,normal,isNew,shift2,true);//use sphere-sphere precompute (with a virtual sphere)
	}
	return true;
}

bool Ig2_Sphere_GridConnection_ScGridCoGeom::goReverse(	const shared_ptr<Shape>& cm1,
								const shared_ptr<Shape>& cm2,
								const State& state1,
								const State& state2,
								const Vector3r& shift2,
								const bool& force,
								const shared_ptr<Interaction>& c)
{
	c->swapOrder();
	return go(cm2,cm1,state2,state1,-shift2,force,c);
}
YADE_PLUGIN((Ig2_Sphere_GridConnection_ScGridCoGeom));


//!##################	Laws   #####################

//!			O/
void Law2_ScGridCoGeom_FrictPhys_CundallStrack::go(shared_ptr<IGeom>& ig, shared_ptr<IPhys>& ip, Interaction* contact){
	int id1 = contact->getId1(), id2 = contact->getId2();

	ScGridCoGeom* geom= static_cast<ScGridCoGeom*>(ig.get());
	FrictPhys* phys = static_cast<FrictPhys*>(ip.get());
	if(geom->penetrationDepth <0){
		if (neverErase) {
			phys->shearForce = Vector3r::Zero();
			phys->normalForce = Vector3r::Zero();}
		else 	scene->interactions->requestErase(contact);
		return;}
	if (geom->isDuplicate) {
		if (id2!=geom->trueInt) {
			//cerr<<"skip duplicate "<<id1<<" "<<id2<<endl;
			if (geom->isDuplicate==2) {/*cerr<<"erase duplicate "<<id1<<" "<<id2<<endl;*/scene->interactions->requestErase(contact);}
		return;}
	}
	Real& un=geom->penetrationDepth;
	phys->normalForce=phys->kn*std::max(un,(Real) 0)*geom->normal;

	Vector3r& shearForce = geom->rotate(phys->shearForce);
	const Vector3r& shearDisp = geom->shearIncrement();
	shearForce -= phys->ks*shearDisp;
	Real maxFs = phys->normalForce.squaredNorm()*std::pow(phys->tangensOfFrictionAngle,2);

	if (likely(!scene->trackEnergy)){//Update force but don't compute energy terms (see below))
		// PFC3d SlipModel, is using friction angle. CoulombCriterion
		if( shearForce.squaredNorm() > maxFs ){
			Real ratio = sqrt(maxFs) / shearForce.norm();
			shearForce *= ratio;}
	} else {
		//almost the same with additional Vector3r instanciated for energy tracing, duplicated block to make sure there is no cost for the instanciation of the vector when traceEnergy==false
		if(shearForce.squaredNorm() > maxFs){
			Real ratio = sqrt(maxFs) / shearForce.norm();
			Vector3r trialForce=shearForce;//store prev force for definition of plastic slip
			//define the plastic work input and increment the total plastic energy dissipated
			shearForce *= ratio;
			Real dissip=((1/phys->ks)*(trialForce-shearForce))/*plastic disp*/ .dot(shearForce)/*active force*/;
			if(dissip>0) scene->energy->add(dissip,"plastDissip",plastDissipIx,/*reset*/false);
		}
		// compute elastic energy as well
		scene->energy->add(0.5*(phys->normalForce.squaredNorm()/phys->kn+phys->shearForce.squaredNorm()/phys->ks),"elastPotential",elastPotentialIx,/*reset at every timestep*/true);
	}
	if (!scene->isPeriodic) {
		Vector3r force = -phys->normalForce-shearForce;
		scene->forces.addForce(id1,force);
		scene->forces.addTorque(id1,(geom->radius1-0.5*geom->penetrationDepth)* geom->normal.cross(force));
		//FIXME : include moment due to axis-contact distance in forces on node
		Vector3r twist = (geom->radius2-0.5*geom->penetrationDepth)* geom->normal.cross(force);
		scene->forces.addForce(geom->id3,(geom->relPos-1)*force);
		scene->forces.addTorque(geom->id3,(1-geom->relPos)*twist);
		if (geom->relPos) { //else we are on node (or on last node - and id3 is junk)
			scene->forces.addForce(geom->id3,(geom->relPos-1)*force);
			scene->forces.addTorque(geom->id3,(1-geom->relPos)*twist);
			scene->forces.addForce(geom->id4,(-geom->relPos)*force);
			scene->forces.addTorque(geom->id4,geom->relPos*twist);
		}
	}
// 		applyForceAtContactPoint(-phys->normalForce-shearForce, geom->contactPoint, id1, de1->se3.position, id2, de2->se3.position);
	else {//FIXME : periodicity not implemented here :
		Vector3r force = -phys->normalForce-shearForce;
		scene->forces.addForce(id1,force);
		scene->forces.addForce(id2,-force);
		scene->forces.addTorque(id1,(geom->radius1-0.5*geom->penetrationDepth)* geom->normal.cross(force));
		scene->forces.addTorque(id2,(geom->radius2-0.5*geom->penetrationDepth)* geom->normal.cross(force));
	}
}
YADE_PLUGIN((Law2_ScGridCoGeom_FrictPhys_CundallStrack));


//!##################	Bounds   #####################

void Bo1_GridConnection_Aabb::go(const shared_ptr<Shape>& cm, shared_ptr<Bound>& bv, const Se3r& se3, const Body* b){
	GridConnection* GC = static_cast<GridConnection*>(cm.get());
	if(!bv){ bv=shared_ptr<Bound>(new Aabb); }
	Aabb* aabb=static_cast<Aabb*>(bv.get());
	if(!scene->isPeriodic){
		const Vector3r& O = se3.position;
		Vector3r O2 = se3.position+GC->getSegment();
		aabb->min=aabb->max=O;
		for (int k=0;k<3;k++){
			aabb->min[k]=min(aabb->min[k],min(O[k],O2[k])-GC->radius);
			aabb->max[k]=max(aabb->max[k],max(O[k],O2[k])+GC->radius);
		}
		return;
	}
}

YADE_PLUGIN((Bo1_GridConnection_Aabb));

#ifdef YADE_OPENGL
//!##################	Rendering   #####################

bool Gl1_GridConnection::wire;
bool Gl1_GridConnection::glutNormalize;
int  Gl1_GridConnection::glutSlices;
int  Gl1_GridConnection::glutStacks;

void Gl1_GridConnection::out( Quaternionr q )
{
	AngleAxisr aa(q);
	std::cout << " axis: " <<  aa.axis()[0] << " " << aa.axis()[1] << " " << aa.axis()[2] << ", angle: " << aa.angle() << " | ";
}

void Gl1_GridConnection::go(const shared_ptr<Shape>& cm, const shared_ptr<State>& st ,bool wire2, const GLViewInfo&)
{	
	GridConnection *GC=static_cast<GridConnection*>(cm.get());
	Real r=GC->radius;
	Real length=GC->getLength();
	Vector3r segt = GC->node2->state->pos - GC->node1->state->pos;
	//glMaterialv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, Vector3f(cm->color[0],cm->color[1],cm->color[2]));
	
	const shared_ptr<Interaction> intr = scene->interactions->find((int)GC->node1->getId(),(int)GC->node2->getId());
	
	glColor3v(cm->color);
	if(glutNormalize) glPushAttrib(GL_NORMALIZE);
// 	glPushMatrix();
	Quaternionr shift;
	shift.setFromTwoVectors(Vector3r::UnitZ(),segt);
	if(intr){drawCylinder(wire || wire2, r,length,shift);}
	if(glutNormalize) glPopAttrib();
// 	glPopMatrix();
	return;
}

void Gl1_GridConnection::drawCylinder(bool wire, Real radius, Real length, const Quaternionr& shift) const
{
   glPushMatrix();
   GLUquadricObj *quadObj = gluNewQuadric();
   gluQuadricDrawStyle(quadObj, (GLenum) (wire ? GLU_SILHOUETTE : GLU_FILL));
   gluQuadricNormals(quadObj, (GLenum) GLU_SMOOTH);
   gluQuadricOrientation(quadObj, (GLenum) GLU_OUTSIDE);
   AngleAxisr aa(shift);
   glRotatef(aa.angle()*180.0/Mathr::PI,aa.axis()[0],aa.axis()[1],aa.axis()[2]);
   gluCylinder(quadObj, radius, radius, length, glutSlices,glutStacks);
   gluQuadricOrientation(quadObj, (GLenum) GLU_INSIDE);
   glutSolidSphere(radius,glutSlices,glutStacks);
   glTranslatef(0.0,0.0,length);

   glutSolidSphere(radius,glutSlices,glutStacks);
//    gluDisk(quadObj,0.0,radius,glutSlices,_loops);
   gluDeleteQuadric(quadObj);
   glPopMatrix();
}
YADE_PLUGIN((Gl1_GridConnection));
#endif