#include "Simulator.h"



Simulator::Simulator(Robot* pRobot, BodyTracker::CalibFunctor CalibCbFun):
	m_pRobot(pRobot),
	m_CalibCbFun(CalibCbFun),
	m_iSpeedUpFactor(1),
	m_iStepLength(100),
	m_iStepCounter(0),
	m_pThreadRobot(NULL),
	m_pThreadKinect(NULL)
	
{
	Config::Instance()->assign("simulator/SpeedUpFactor", m_iSpeedUpFactor);
	Config::Instance()->assign("simulator/StepLength", m_iStepLength); // in milliseconds
	Config::Instance()->assign("simulator/KinectDataFilePath", m_StrKinectDataFilePath);
	m_KinectDataReader.openFile(m_StrKinectDataFilePath.c_str());
	m_pThreadRobot = new std::thread(&Simulator::threadProcRobot, this);
	m_pThreadKinect = new std::thread(&Simulator::threadProcKinect, this);
	
}


Simulator::~Simulator()
{
	delete m_pThreadKinect;
	delete m_pThreadRobot;
}

void Simulator::threadProcRobot()
{
	while (1) {
		pcRobotState pcrs = m_pRobot->getState();
		INT64 tsWindows = GetTickCount64();
		

		float v, w, th;
		

		if (pcrs->isFollowing || pcrs->isCalibrating)
		{
			m_pRobot->calcControl(&v, &w);
		}
		else
		{
			v = 0;
			w = 0;
		}
		updateState(v, w);

		m_iStepCounter++;
		std::this_thread::sleep_for(std::chrono::milliseconds(m_iStepLength / m_iSpeedUpFactor));
	}
}

void Simulator::updateState(float v, float w)
{
	const float aMax = 0.3, alphaMax = 1.5;
	const float vMax = 0.75, wMax = 1.0;
	RobotState & rs = m_pRobot->m_State;
	float dt;
	if (rs.tsWindows == -1) {
		rs.tsWindows = GetTickCount64();
		return;
	}
	else {
		dt = m_iStepLength / 1000.0;
		rs.tsWindows += m_iStepLength;
	}

	// Update v and w
	const float vOld = rs.v, wOld = rs.w;
	rs.v += saturate(v - rs.v, -aMax * dt, aMax * dt);
	rs.v = saturate(rs.v, -vMax, vMax);
	rs.w += saturate(w - rs.w, -alphaMax * dt, alphaMax * dt);
	rs.w = saturate(rs.w, -wMax, wMax);
	
	// update th
	const float thOld = rs.th;
	rs.th += (wOld + rs.w) / 2 * dt;
	if (rs.th > M_PI) rs.th -= 2 * M_PI;
	else if (rs.th <= -M_PI) rs.th += 2 * M_PI;

	// Update x and y
	const float xOld = rs.x, yOld = rs.y;
	float dXY = (vOld + rs.v) / 2 * dt;
	rs.x += dXY * cos((thOld + rs.th) / 2);
	rs.y += dXY * sin((thOld + rs.th) / 2);

	// Update virtual marker
	float xVmNew = rs.x + m_pRobot->m_Params.VmDistance * cos(rs.th + m_pRobot->m_Params.VmHeading);
	float yVmNew = rs.y + m_pRobot->m_Params.VmDistance * sin(rs.th + m_pRobot->m_Params.VmHeading);

	static bool is_first = true;
	if (is_first) {
		m_pRobot->m_pPath->setPathPose(xVmNew, yVmNew, 0);
		m_pRobot->resetVisualCmd();
		m_pRobot->recordDesiredPath();
		is_first = false;
	}
	else {
		float dxVm = xVmNew - rs.xVm;
		float dyVm = yVmNew - rs.yVm;
		float distGuess = rs.dist + pow(pow(dxVm, 2) + pow(dyVm, 2), 0.5);

		if (0 && m_pRobot->m_pPath) {
			// Golden-section search for the closest point on the path
			const float guessRange = 0.1f, goldenRatio = 1.618f;
			const float tolerance = 5e-3;
			float a = distGuess - guessRange;
			float b = distGuess + guessRange;
			float c = b - (b - a) / goldenRatio;
			float d = a + (b - a) / goldenRatio;
			while (fabs(c - d) > tolerance) {
				geometry_msgs::Pose * pPoseDesired;
				pPoseDesired = m_pRobot->m_pPath->getPoseOnPath(c);
				float fc = sqrt(pow(pPoseDesired->position.x - rs.xVm, 2) + pow(pPoseDesired->position.y - rs.yVm, 2));
				pPoseDesired = m_pRobot->m_pPath->getPoseOnPath(d);
				float fd = sqrt(pow(pPoseDesired->position.x - rs.xVm, 2) + pow(pPoseDesired->position.y - rs.yVm, 2));
				if (fc < fd) b = d;
				else a = c;
				c = b - (b - a) / goldenRatio;
				d = a + (b - a) / goldenRatio;
			}
			distGuess = (b + a) / 2;
		}
		rs.dist = distGuess;
	}

	rs.xVm = xVmNew;
	rs.yVm = yVmNew;

	m_pRobot->log();

	if (m_pRobot->SIPcbFun && m_iStepCounter%m_iSpeedUpFactor ==0) {
		m_pRobot->SIPcbFun();
	}
}

// TO-DO: read kinect data files
void Simulator::threadProcKinect()
{
	while (1) {
		BodyTracker::Vector3d pointLA(-1, 0, 0);
		BodyTracker::Vector3d pointRA(-1, 0.2, 0);
		if (m_CalibCbFun)
			m_CalibCbFun(pointLA, pointRA);
		std::this_thread::sleep_for(std::chrono::milliseconds(m_iStepLength / 3 / m_iSpeedUpFactor));
	}
}
