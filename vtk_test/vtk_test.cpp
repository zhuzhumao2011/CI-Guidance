#include "vtk_test.h"
#include "patient_reg_widget.h"

 #include <vtkAutoInit.h>
 VTK_MODULE_INIT(vtkRenderingOpenGL);
 VTK_MODULE_INIT(vtkInteractionStyle);

#include <qscreen.h>
#include <QVTKWidget.h>
#include <qfiledialog.h>
#include <vtkCamera.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkOBJReader.h>
#include <vtkSTLReader.h>
#include <vtkPolyDataMapper.h>
#include <Conversions.h>
#include <vtkTransform.h>
#include <vtkSmartPointer.h>
#include <vtkProperty.h>
#include <vtkTriangleFilter.h>
#include <vtkStripper.h>
#include <math.h>
#include <Eigen/Dense>
#include <Eigen/Geometry>
#include "infowidget.h"
#include <cassert>
#include <boost/ptr_container/ptr_vector.hpp>

#ifndef NDEBUG
#define PRINT_MATRIX(mat) {std::cout << #mat ##":" << std::endl << mat << std::endl;}
#else
#define PRINT_MATRIX(mat) (mat);
#endif

#define	 TRACKER_SIMULATE		0		// 1 for simulate, 0 if using tracker
#define  TRACKER_COMPORT		7		// NOTE: COM port is zero-indexed (N-1)
#define	 PROBE_DESIRED_X		-79.3	// fixed position to use as the target pose
#define	 PROBE_DESIRED_Y		-376.8	
#define	 PROBE_DESIRED_Z		1451.95
#define  SIMULATE_ERROR			0.9		// Error to use when simulating

 const static int FRAME_RATE_UPDATE_INTERVAL = 1000; //integer [ms]
 const static int NUM_TRACKED_TOOLS = 2; // which tools depends on the order in the .ini file (first N tools)

vtk_test::vtk_test(QWidget *parent)
	: QMainWindow(parent),
	 m_time(0),
	 m_tracker(TRACKER_COMPORT),
	 m_frames(0)
{
	ui.setupUi(this);
	
	double dpi = QApplication::screens().at(0)->physicalDotsPerInch();

	InfoWidget *iw = new InfoWidget(this);
	ui.gridlayout->addWidget(iw,0,0,2,1);
	iw->setMaximumSize( (int)(2*dpi), (int)(25*dpi));

	m_pQVTK_top= new QVTKWidget(this);
	ui.gridlayout->addWidget(m_pQVTK_top,0,1,1,1,0);

	m_pQVTK_oblique = new QVTKWidget(this);
	ui.gridlayout->addWidget(m_pQVTK_oblique,0,2,1,1,0);

	m_pQVTK_front = new QVTKWidget(this);
	ui.gridlayout->addWidget(m_pQVTK_front,1,1,1,1,0);

	m_pQVTK_side = new QVTKWidget(this);
	ui.gridlayout->addWidget(m_pQVTK_side,1,2,1,1,0);

	m_pRenderer_oblique = vtkRenderer::New();
	m_pRenderer_top = vtkRenderer::New();
	m_pRenderer_front = vtkRenderer::New();
	m_pRenderer_side = vtkRenderer::New();

	m_pQVTK_top->setMinimumSize( (int)(3*dpi), (int)(3*dpi) );
	m_pQVTK_top->GetRenderWindow()->AddRenderer(m_pRenderer_top);

	m_pQVTK_oblique->setMinimumSize( (int)(3*dpi), (int)(3*dpi) );
	m_pQVTK_oblique->GetRenderWindow()->AddRenderer(m_pRenderer_oblique);

	m_pQVTK_front->setMinimumSize( (int)(3*dpi), (int)(3*dpi) );
	m_pQVTK_front->GetRenderWindow()->AddRenderer(m_pRenderer_front);

	m_pQVTK_side->setMinimumSize( (int)(3*dpi), (int)(3*dpi) );
	m_pQVTK_side->GetRenderWindow()->AddRenderer(m_pRenderer_side);
	
	m_pRenderer_oblique->SetBackground(0.8,0.8,0.8);
	m_pRenderer_top->SetBackground(0.8,0.8,0.8);
	m_pRenderer_front->SetBackground(0.8,0.8,0.8);
	m_pRenderer_side->SetBackground(0.8,0.8,0.8);
	m_pRenderer_oblique->SetGradientBackground(true);
	m_pRenderer_top->SetGradientBackground(true);
	m_pRenderer_front->SetGradientBackground(true);
	m_pRenderer_side->SetGradientBackground(true);

		// Set camera views
	vtkSmartPointer<vtkCamera> camera_top = vtkSmartPointer<vtkCamera>::New();
	camera_top->SetPosition(0,100,1575);
	camera_top->SetFocalPoint(0,-300,1575);
	camera_top->SetViewUp(0,0,-1);
	camera_top->SetClippingRange(10.0,10000.0);
	m_pRenderer_top->SetActiveCamera(camera_top);

	vtkSmartPointer<vtkCamera> camera_front = vtkSmartPointer<vtkCamera>::New();
	camera_front->SetPosition(0,-250,2200);
	camera_front->SetFocalPoint(0,-250,1700);
	camera_front->SetViewUp(0,1,0);
	camera_front->SetClippingRange(10.0,10000.0);
	m_pRenderer_front->SetActiveCamera(camera_front);

	vtkSmartPointer<vtkCamera> camera_side = vtkSmartPointer<vtkCamera>::New();
	camera_side->SetPosition(800,-250,1575);
	camera_side->SetFocalPoint(200,-250,1575);
	camera_side->SetViewUp(0,1,0);
	camera_side->SetClippingRange(10.0,10000.0);
	m_pRenderer_side->SetActiveCamera(camera_side);

	m_pRenderer_oblique->Delete();
	m_pRenderer_top->Delete();
	m_pRenderer_front->Delete();
	m_pRenderer_side->Delete();

	m_timer.setInterval(0);
	m_timer.setSingleShot(false);
	connect(&m_timer, SIGNAL(timeout()), this, SLOT(slot_onGUITimer()));
	m_timer.start();

	m_frameRateTimer.setInterval(FRAME_RATE_UPDATE_INTERVAL);
	m_frameRateTimer.setSingleShot(false);
	connect(&m_frameRateTimer, SIGNAL(timeout()), this, SLOT(slot_onFrameRateTimer()));
	m_frameRateTimer.start();

	m_tracker.InitializeSystem();
	m_tracker.StartTracking();
		
	connect(ui.actionRegister_Patient, SIGNAL(triggered()),this, SLOT(slot_Register_Patient()));
	connect(this, SIGNAL(sgn_NewProbePosition(double,double,double)), iw, SLOT(slot_NewProbePosition(double,double,double)));
	connect(this, SIGNAL(sgn_NewCIPosition(double,double,double)), iw, SLOT(slot_NewCIPosition(double,double,double)));
	connect(this, SIGNAL(sgn_NewMagPosition(double,double,double)), iw, SLOT(slot_NewMagPosition(double,double,double)));
	connect(this, SIGNAL(sgn_err(double,double)), iw, SLOT(slot_update_err(double,double)));
	
	ui.statusBar->addWidget( &m_frameRateLabel );
}



vtk_test::~vtk_test()
{
	m_tracker.StopTracking();
}

void vtk_test::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	m_frameRateLabel.setGeometry( m_pQVTK_top->geometry() );

}

void vtk_test::slot_onFrameRateTimer()
{
	double rate = (double)m_frames*1000.0 / (double)FRAME_RATE_UPDATE_INTERVAL;
	m_frameRateLabel.setText( QString::number((int)rate).rightJustified(4,' ',false) );
	m_frames = 0;
}

void vtk_test::slot_onRegistration(Eigen::MatrixXd T)
{
	std::cout << T << std::endl;
}

void vtk_test::slot_onGUITimer()
{
	// declare variables	
	//std::vector<Eigen::Quaterniond, Eigen::aligned_allocator<Eigen::Quaterniond> > quat_Polaris;
	//std::vector<Eigen::Vector3d, Eigen::aligned_allocator<Eigen::Vector3d> > p;
	//std::vector<Eigen::Matrix3d, Eigen::aligned_allocator<Eigen::Matrix3d> > R; 
	//std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d> > T;
	//std::vector<Eigen::Matrix4d, Eigen::aligned_allocator<Eigen::Matrix4d> > Trans_final;
	boost::ptr_vector< Eigen::Quaterniond > quat_Polaris;
	boost::ptr_vector< Eigen::Vector3d > p;
	boost::ptr_vector< Eigen::Matrix3d> R;
	boost::ptr_vector< Eigen::Matrix4d > T;
	boost::ptr_vector< Eigen::Matrix4d > Trans_final;

	quat_Polaris.resize( NUM_TRACKED_TOOLS+1 );
	p.resize( NUM_TRACKED_TOOLS+1 );
	R.resize( NUM_TRACKED_TOOLS+1 );
	T.resize( NUM_TRACKED_TOOLS+1 );
	Trans_final.resize( NUM_TRACKED_TOOLS+1 );

	vtkSmartPointer<vtkTransform> pvtk_T_probe = vtkSmartPointer<vtkTransform>::New();
	vtkSmartPointer<vtkTransform> pvtk_T_CItool = vtkSmartPointer<vtkTransform>::New();

	// get transformations from tracker and place in eigen matrices
	std::vector<ToolInformationStruct> tools = m_tracker.GetTransformations();

	if(TRACKER_SIMULATE){
		quat_Polaris[1] = Eigen::Quaterniond(1, 0, 0, 0);
		p[1](0) = -PROBE_DESIRED_Y+2;	// Simulated position in Polaris Frame
		p[1](1) = -PROBE_DESIRED_X-4;
		p[1](2) = -PROBE_DESIRED_Z+6;
		p[2](0) = -PROBE_DESIRED_Y+3;
		p[2](1) = -PROBE_DESIRED_X+4;
		p[2](2) = -PROBE_DESIRED_Z-2;
	}
	else{
		for (int toolnum = 1; toolnum <=NUM_TRACKED_TOOLS; toolnum++){
			quat_Polaris[toolnum] = Eigen::Quaterniond(tools[toolnum].q0, tools[toolnum].qx, tools[toolnum].qy, tools[toolnum].qz);
			p[toolnum](0) = tools[toolnum].x;
			p[toolnum](1) = tools[toolnum].y;
			p[toolnum](2) = tools[toolnum].z;
		}
	}

	// convert quaternion to rotation matrix and combine with translation into a transformation matrix
	for (int toolnum = 1; toolnum <=NUM_TRACKED_TOOLS; toolnum++){
		R[toolnum] = quat_Polaris[toolnum].toRotationMatrix();
		T[toolnum] = Eigen::Matrix4d::Identity();
		T[toolnum].block<3,3>(0,0) = R[toolnum];
		T[toolnum].block<3,1>(0,3) = p[toolnum];
		Eigen::Matrix4d Polaris_sim_trans(4,4);
		Polaris_sim_trans << 0,-1, 0, 0, // note: inverse is equal to itself for this matrix
							-1, 0, 0, 0,
							 0, 0,-1, 0,
							 0, 0, 0, 1;

		// apply similarity transform
		Trans_final[toolnum] = Polaris_sim_trans*T[toolnum]*Polaris_sim_trans.inverse();

		Trans_final[toolnum] = Trans_final[toolnum].transpose().eval();
	}
	pvtk_T_probe->SetMatrix( Trans_final[1].data() );
	pvtk_T_CItool->SetMatrix( Trans_final[2].data() );
	m_pActor_probe->SetUserTransform(pvtk_T_probe);
	m_pActor_CItool->SetUserTransform(pvtk_T_CItool);

	//m_pRenderer_oblique->ResetCameraClippingRange();
	//m_pRenderer_top->ResetCameraClippingRange();
	//m_pRenderer_front->ResetCameraClippingRange();
	//m_pRenderer_side->ResetCameraClippingRange();

	m_pQVTK_top->update();
	m_pQVTK_oblique->update();
	m_pQVTK_front->update();
	m_pQVTK_side->update();
	
	if(!TRACKER_SIMULATE)
		Update_err(tools);
	else
	{
		double tip_err = SIMULATE_ERROR;
		emit sgn_err(tip_err, 5);
	}

	emit sgn_NewProbePosition( Trans_final[1](3,0), Trans_final[1](3,1), Trans_final[1](3,2) );
	emit sgn_NewCIPosition( Trans_final[2](3,0), Trans_final[2](3,1), Trans_final[2](3,2) );
	m_frames++;
}

void vtk_test::Update_err(std::vector<ToolInformationStruct> const& tools)
{
	if (CI_entry.rows() > 0)	// check if value has been set (via registration)
	{
		double tip_err = sqrt( pow((-tools[2].y - (CI_entry(0,0))),2) + 
							   pow((-tools[2].x - (CI_entry(0,1))),2) +
							   pow((-tools[2].z - (CI_entry(0,2))),2) );
		emit sgn_err(tip_err, 100);
	}

	
	//double tip_err = sqrt( pow((-tools[2].y - (PROBE_DESIRED_X)),2) + 
	//					   pow((-tools[2].x - (PROBE_DESIRED_Y)),2) +
	//					   pow((-tools[2].z - (PROBE_DESIRED_Z)),2) );
}

void SetTransformForCI_target( vtkSmartPointer<vtkActor> pActor_CI_target )
{
	Eigen::MatrixXd T = Eigen::MatrixXd::Identity(4,4);
	T(0,3) = PROBE_DESIRED_X;
	T(1,3) = PROBE_DESIRED_Y;
	T(2,3) = PROBE_DESIRED_Z;

	T = T.transpose().eval();

	vtkSmartPointer<vtkTransform> vtkT = vtkSmartPointer<vtkTransform>::New();
	vtkT->SetMatrix( T.data() );
	pActor_CI_target->SetUserTransform( vtkT );
}

void vtk_test::SetTransformforCI_target(patient_data ref_patient_data, Eigen::MatrixXd T_registration)
{
	Eigen::MatrixXd target = ref_patient_data.GetTarget();	// returns a 1x3 row
	Eigen::MatrixXd entry = ref_patient_data.GetEntry();
	CI_entry = entry;
	Eigen::MatrixXd target_reg(4,1); // create new vectors for storing the registered positions
	Eigen::MatrixXd entry_reg(4,1); 

	target_reg.block<3,1>(0,0) = target.transpose(); // convert row to column
	target_reg(3,0) = 1;
	entry_reg.block<3,1>(0,0) = entry.transpose();
	entry_reg(3,0) = 1;
	
	// apply registration to bring into camera frame
	target_reg = T_registration*target_reg;
	entry_reg = T_registration*entry_reg;

	// move to origin and calculate axis-angle rotation w.r.t. vertical (default orientation for the CI tool)
	Eigen::MatrixXd temp = entry.transpose()-target.transpose();
	Eigen::Vector3d traj = temp.block<3,1>(0,0);
	Eigen::Vector3d yaxis(0,1,0);
	Eigen::Vector3d axis = traj.cross(yaxis);
	axis = axis/axis.norm();
	double angle = acos(traj.dot(yaxis)/traj.norm());
	Eigen::AngleAxisd angleaxis(-angle,axis);

	// assemble transformation matrix
	Eigen::Matrix3d R = angleaxis.toRotationMatrix();
	Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
	T.block<3,3>(0,0) = R;
	T.block<4,1>(0,3) = entry_reg;
	T = T.transpose().eval();

	PRINT_MATRIX(T);

	vtkSmartPointer<vtkTransform> vtkT_CI = vtkSmartPointer<vtkTransform>::New();
	vtkT_CI->SetMatrix(T.data());
	m_pActor_CItarget->SetUserTransform( vtkT_CI );
}

void vtk_test::Initialize()
{	
	double color1[] = {0.8,0.3,0.3};
	vtkSmartPointer<vtkActor> pActor_CI_target = LoadOBJFile(QString::fromLocal8Bit("D:\\Trevor\\My Documents\\Code\\VTKtest\\vtk_test\\x64\\Release\\insertion_tool_wtarget.obj"), 0.3, color1);
	//SetTransformForCI_target(pActor_CI_target);
	m_pRenderer_oblique->AddActor(pActor_CI_target);
	m_pRenderer_top->AddActor(pActor_CI_target);
	m_pRenderer_front->AddActor(pActor_CI_target);
	m_pRenderer_side->AddActor(pActor_CI_target);

	double color2[] = {0.3,1.0,0.3};
	vtkSmartPointer<vtkActor> pActor_CI_tool = LoadOBJFile(QString::fromLocal8Bit("D:\\Trevor\\My Documents\\Code\\VTKtest\\vtk_test\\x64\\Release\\insertion_tool_wtarget.obj"), 1.0, color2);
	m_pRenderer_oblique->AddActor(pActor_CI_tool);
	m_pRenderer_top->AddActor(pActor_CI_tool);
	m_pRenderer_side->AddActor(pActor_CI_tool);
	m_pRenderer_front->AddActor(pActor_CI_tool);

	double color3[] = {0.3,0.3,1};
	vtkSmartPointer<vtkActor> pActor_probe = LoadOBJFile(QString::fromLocal8Bit("D:\\Trevor\\My Documents\\Code\\VTKtest\\vtk_test\\x64\\Release\\polaris_probe.obj"), 1.0, color3);
	//vtkSmartPointer<vtkActor> pActor_probe = LoadSTLFile(QString::fromLocal8Bit("C:\\Users\\wirzgor\\Source\\Repos\\CI-Guidance\\x64\\Debug\\patient.stl"), 1.0, color3);
	m_pRenderer_oblique->AddActor(pActor_probe);
	m_pRenderer_top->AddActor(pActor_probe);
	m_pRenderer_front->AddActor(pActor_probe);
	m_pRenderer_side->AddActor(pActor_probe);

	m_pActor_CItarget = pActor_CI_target;
	m_pActor_CItool = pActor_CI_tool;
	m_pActor_probe = pActor_probe;
}


vtkSmartPointer<vtkActor> vtk_test::LoadOBJFile(QString const& str,double opacity, double color[3]) const
{
	vtkOBJReader *reader = vtkOBJReader::New();
	reader->SetFileName( str.toLocal8Bit().data() );
	
	vtkTriangleFilter *tris = vtkTriangleFilter::New();
	tris->SetInputConnection( reader->GetOutputPort() );
	tris->GetOutput()->GlobalReleaseDataFlagOn();

	vtkStripper *stripper = vtkStripper::New();
	stripper->SetInputConnection( tris->GetOutputPort() );
	stripper->GetOutput()->GlobalReleaseDataFlagOn();

	vtkPolyDataMapper *mapper = vtkPolyDataMapper::New();
	mapper->SetInputConnection( stripper->GetOutputPort() );
	mapper->ReleaseDataFlagOn();

	vtkSmartPointer<vtkActor> pActor = vtkSmartPointer<vtkActor>::New();
	//m_pActor = vtkActor::New();
	pActor->GetProperty()->SetOpacity(opacity);
	pActor->GetProperty()->SetColor(color);
	pActor->SetMapper(mapper);

	reader->Delete();
	mapper->Delete();
	tris->Delete();
	stripper->Delete();

	return pActor;
}

vtkSmartPointer<vtkActor> vtk_test::LoadSTLFile(QString const& str, double opacity, double color[3]) const
{
	vtkSTLReader *reader = vtkSTLReader::New();
	reader->SetFileName(str.toLocal8Bit().data());

	vtkTriangleFilter *tris = vtkTriangleFilter::New();
	tris->SetInputConnection(reader->GetOutputPort());
	tris->GetOutput()->GlobalReleaseDataFlagOn();

	vtkStripper *stripper = vtkStripper::New();
	stripper->SetInputConnection(tris->GetOutputPort());
	stripper->GetOutput()->GlobalReleaseDataFlagOn();

	vtkPolyDataMapper *mapper = vtkPolyDataMapper::New();
	mapper->SetInputConnection(stripper->GetOutputPort());
	mapper->ReleaseDataFlagOn();

	vtkSmartPointer<vtkActor> pActor = vtkSmartPointer<vtkActor>::New();
	//m_pActor = vtkActor::New();
	pActor->GetProperty()->SetOpacity(opacity);
	pActor->GetProperty()->SetColor(color);
	pActor->SetMapper(mapper);

	reader->Delete();
	mapper->Delete();
	tris->Delete();
	stripper->Delete();

	return pActor;
}

void vtk_test::slot_Register_Patient()
{
	// TO DO: insert pop up file browser to select patient data
	QString fileName = QFileDialog::getOpenFileName(NULL,QString("Open Patient Data File"));
	cout << fileName.toLocal8Bit().data() << endl;

	// load patient data
	patient_data ref_patient_data;

	Pat_Reg_Widget Pat_Reg_Widget(ref_patient_data);
	connect(this, SIGNAL(sgn_NewProbePosition(double,double,double) ), &Pat_Reg_Widget, SLOT(slot_onNewProbePosition(double,double,double)));
	Pat_Reg_Widget.exec();
	RigidRegistration reg = Pat_Reg_Widget.GetRegistration();
	cout << reg.GetTransform() << endl;
	SetTransformforCI_target(ref_patient_data, reg.GetTransform());
	//m_pRenderer_oblique->AddActor(m_pActor_CItarget);
	//m_pRenderer_top->AddActor(m_pActor_CItarget);
	//m_pRenderer_front->AddActor(m_pActor_CItarget);
	//m_pRenderer_side->AddActor(m_pActor_CItarget);
}
