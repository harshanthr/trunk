# -*- coding: utf-8 -*-
# Here, we are testing bulk modulus, then permeability, then the consolidation of a specimen.
# the test is based on examples/FluidCouplingPFV/oedometer.py, only slightly simplified and using less particles

try: FlowEngine
except NameError:
	print "skip DEM-PFV check, FlowEngine not available"
else:

	errors=0
	tolerance=0.01

	from yade import pack
	num_spheres=100# number of spheres
	young=1e6
	compFricDegree = 3 # initial contact friction during the confining phase
	finalFricDegree = 30 # contact friction during the deviatoric loading
	mn,mx=Vector3(0,0,0),Vector3(1,1,1) # corners of the initial packing

	O.materials.append(FrictMat(young=young,poisson=0.5,frictionAngle=radians(compFricDegree),density=2600,label='spheres'))
	O.materials.append(FrictMat(young=young,poisson=0.5,frictionAngle=0,density=0,label='walls'))
	walls=aabbWalls([mn,mx],thickness=0,material='walls')
	wallIds=O.bodies.append(walls)

	sp=pack.SpherePack()
	sp.makeCloud(mn,mx,-1,0.3333,num_spheres,False, 0.95,seed=1) #"seed" make the "random" generation always the same
	sp.toSimulation(material='spheres')

	triax=TriaxialStressController(
		maxMultiplier=1.+2e4/young, # spheres growing factor (fast growth)
		finalMaxMultiplier=1.+2e3/young, # spheres growing factor (slow growth)
		thickness = 0,
		stressMask = 7,
		max_vel = 0.005,
		internalCompaction=True, # If true the confining pressure is generated by growing particles
	)

	newton=NewtonIntegrator(damping=0.2)

	O.engines=[
		ForceResetter(),
		InsertionSortCollider([Bo1_Sphere_Aabb(),Bo1_Box_Aabb()]),
		InteractionLoop(
			[Ig2_Sphere_Sphere_ScGeom(),Ig2_Box_Sphere_ScGeom()],
			[Ip2_FrictMat_FrictMat_FrictPhys()],
			[Law2_ScGeom_FrictPhys_CundallStrack()],label="iloop"
		),
		FlowEngine(dead=1,label="flow"),#introduced as a dead engine for the moment, see 2nd section
		GlobalStiffnessTimeStepper(active=1,timeStepUpdateInterval=100,timestepSafetyCoefficient=0.8),
		triax,
		newton
	]

	triax.goal1=triax.goal2=triax.goal3=10000

	while 1:
		O.run(200, True)
		unb=unbalancedForce()
		if unb<0.01 and abs(10000-triax.meanStress)/10000<0.01: break

	setContactFriction(radians(finalFricDegree))

	## ______________   Oedometer section   _________________

	#A. Check bulk modulus of the dry material from load/unload cycles
	triax.stressMask=2
	triax.goal1=triax.goal3=0

	triax.internalCompaction=False
	triax.wall_bottom_activated=False
	triax.goal2=11000; O.run(2000,1)
	triax.goal2=10000; O.run(2000,1)
	triax.goal2=11000; O.run(2000,1)
	e22=triax.strain[1]
	triax.goal2=10000; O.run(2000,1)

	e22=e22-triax.strain[1]
	modulus = 1000./abs(e22)

	target=263673.1423
	if abs((modulus-target)/target)>tolerance :
		print "DEM-PFV: difference in bulk modulus:", modulus, "vs. target ",target
		errors+=1

	#B. Activate flow engine and set boundary conditions in order to get permeability
	flow.dead=0
	flow.defTolerance=0.3
	flow.meshUpdateInterval=200
	flow.useSolver=3
	flow.viscosity=10
	flow.bndCondIsPressure=[0,0,1,1,0,0]
	flow.bndCondValue=[0,0,1,0,0,0]
	flow.boundaryUseMaxMin=[0,0,0,0,0,0]
	O.dt=0.1e-3
	O.dynDt=False

	O.run(1,1)
	Qin = flow.getBoundaryFlux(2)
	Qout = flow.getBoundaryFlux(3)
	permeability = abs(Qin)/1.e-4 #size is one, we compute K=V/∇H

	if abs(Qin+Qout)>1e-15 :
		print "DEM-PFV: unbalanced Qin vs. Qout"
		errors+=1

	target=0.0512650663801
	if abs((permeability-target)/target)>tolerance :
		print "DEM-PFV: difference in permeability:",permeability," vs. target ",target
		errors+=1

	#C. now the oedometer test, drained at the top, impermeable at the bottom plate
	flow.bndCondIsPressure=[0,0,0,1,0,0]
	flow.bndCondValue=[0,0,0,0,0,0]
	newton.damping=0

	zeroTime=O.time
	zeroe22 = triax.strain[1]
	triax.goal2=11000

	O.timingEnabled=1
	from yade import timing
	O.run(3000,1)

	target=528.554831762
	if abs((flow.getPorePressure((0.5,0.1,0.5))-target)/target)>tolerance :
		print "DEM-PFV: difference in final pressure:",flow.getPorePressure((0.5,0.1,0.5))," vs. target ",target
		errors+=1
	target=0.00265188596144
	if abs((triax.strain[1]-zeroe22-target)/target)>tolerance :
		print "DEM-PFV: difference in final deformation",triax.strain[1]-zeroe22," vs. target ",target
		errors+=1

	if (float(flow.execTime)/float(sum([e.execTime for e in O.engines])))>0.6 :
		print "DEM-PFV: More than 60\% of cpu time in FlowEngine (",100.*(float(flow.execTime)/float(sum([e.execTime for e in O.engines]))) ,"%). Should not happen with efficient libraries (check blas/lapack/cholmod implementations)"
		errors+=1

	if (errors):
		resultStatus +=1	#Test is failed