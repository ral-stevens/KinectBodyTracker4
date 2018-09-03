//------------------------------------------------------------------------------
// <copyright file="BodyBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <strsafe.h>
#include "resource.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <string>
#include <Windows.h>
#include "RosPublisher.h" // Deprecated
#include "BodyBasics.h"
#include "Config.h"
#include "robot.h"

static const float c_JointThickness = 3.0f;
static const float c_TrackedBoneThickness = 6.0f;
static const float c_InferredBoneThickness = 1.0f;
static const float c_HandSize = 30.0f;

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(    
	_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    CBodyBasics application;
    application.Run(hInstance, nShowCmd);
}

/// <summary>
/// Constructor
/// </summary>
CBodyBasics::CBodyBasics() :
    m_hWnd(NULL),
    m_nStartTime(0),
    m_nLastCounter(0),
    m_nFramesSinceUpdate(0),
    m_fFreq(0),
    m_nNextStatusTime(0LL),
    m_pKinectSensor(NULL),
    m_pCoordinateMapper(NULL),
    m_pBodyFrameReader(NULL),
    m_pD2DFactory(NULL),
    m_pRenderTarget(NULL),
    m_pBrushJointTracked(NULL),
    m_pBrushJointInferred(NULL),
    m_pBrushBoneTracked(NULL),
    m_pBrushBoneInferred(NULL),
    m_pBrushHandClosed(NULL),
    m_pBrushHandOpen(NULL),
    m_pBrushHandLasso(NULL),
	m_pRosPublisher(NULL),
	m_pConfig(NULL),
	m_pRobot(NULL),
	m_pCalibKinectFile(NULL),
	m_pSyncSocket(NULL)
{
    LARGE_INTEGER qpf = {0};
    if (QueryPerformanceFrequency(&qpf))
    {
        m_fFreq = double(qpf.QuadPart);
    }

	// Open a csv file
	std::string fileNameKinect;
	generateFileName(fileNameKinect, "Kinect");
	m_pKinectFile = new std::ofstream(fileNameKinect, std::ofstream::out); // Open the csv file
	
	//*m_pKinectFile << "123" << std::endl; // Test write

	m_pConfig = new Config();
	m_pSyncSocket = new SyncSocket();
	m_pRobot = new Robot();

	m_pConfig->load();
	setParams();

	ZeroMemory(&m_JointData, sizeof(m_JointData));
	m_JointData.tsWindowsBase = GetTickCount64();
	int i = 0;
	for (auto &jt : jointTypeMap)
	{
		m_JointData.names[i++] = std::string(jt.second) + "X";
		m_JointData.names[i++] = std::string(jt.second) + "Y";
		m_JointData.names[i++] = std::string(jt.second) + "Z";
	}
	log(m_pKinectFile, true); //log header
	
}
  

/// <summary>
/// Destructor
/// </summary>
CBodyBasics::~CBodyBasics()
{
	delete m_pRobot;
	delete m_pSyncSocket;
	delete m_pConfig;
	//delete m_pRosPublisher;

	m_pKinectFile->close();
	delete m_pKinectFile;

	if (m_pCalibKinectFile)
	{
		m_pCalibKinectFile->close();
		delete m_pCalibKinectFile;
		m_pCalibKinectFile = NULL;
	}
		

    DiscardDirect2DResources();

    // clean up Direct2D
    SafeRelease(m_pD2DFactory);

    // done with body frame reader
    SafeRelease(m_pBodyFrameReader);

    // done with coordinate mapper
    SafeRelease(m_pCoordinateMapper);

    // close the Kinect Sensor
    if (m_pKinectSensor)
    {
        m_pKinectSensor->Close();
    }

    SafeRelease(m_pKinectSensor);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CBodyBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc;

    // Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"BodyBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        NULL,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)CBodyBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

	RECT rc;
	GetWindowRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rc);
	int heightButton = 60, widthButton = 330, 
		xButton = rc.right + 20, yButton = 20;
	int ySep = 20;

	struct ControlProperty {
		HWND * phWnd;
		LPCWSTR className;
		LPCWSTR text;
		DWORD dwStyle;
		int w; 
		int h;
	};
	
	ControlProperty CPs[] = { 
		{ &m_hWndButtonFollow, L"BUTTON", L"Start following",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, widthButton, heightButton * 5 },

		{ &m_hWndButtonCalibrate, L"BUTTON", L"Start Calibration",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, widthButton, heightButton },

		{ &m_hWndButtonOpenConfig, L"BUTTON", L"Open Config", 
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, widthButton, heightButton },

		{ &m_hWndButtonLoad, L"BUTTON", L"Load Config", 
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, widthButton, heightButton },

		{ &m_hWndStatic, L"STATIC", L"", 
		WS_VISIBLE | WS_CHILD, widthButton, heightButton*2 }
	};

	for (int i = 0, x = xButton, y = yButton;
		i < sizeof(CPs) / sizeof(CPs[0]); 
		y += CPs[i].h + ySep, i++)
	{
		*(CPs[i].phWnd) = CreateWindow(
			CPs[i].className,  // Predefined class; Unicode assumed 
			CPs[i].text,      // Button text 
			CPs[i].dwStyle,  // Styles 
			x,			// x position 
			y,			// y position 
			CPs[i].w,		// Button width
			CPs[i].h,		// Button height
			hWndApp,    // Parent window
			NULL,       // No menu.
			(HINSTANCE)GetWindowLong(hWndApp, GWL_HINSTANCE),
			NULL);      // Pointer not needed.;
	}

	// Register hotkeys
	RegisterHotKey(hWndApp, 1, MOD_CONTROL | MOD_NOREPEAT, 'F');
	RegisterHotKey(hWndApp, 2, MOD_CONTROL | MOD_NOREPEAT, 'C');

    // Show window
    ShowWindow(hWndApp, nCmdShow);
	
    // Main message loop
    while (WM_QUIT != msg.message)
    {
		// Odroid Timestamp
		if (m_pSyncSocket)
			m_pSyncSocket->receive();

		Update();
#ifdef ROBOT_USE_MOTION_COMMAND_FUNCTIONS
		control();
#endif // ROBOT_USE_MOTION_COMMAND_ACTIONS

		//calibrate();

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
			if (WM_QUIT == msg.message) break;
            // If a dialog message will be taken care of by the dialog proc
            if (hWndApp && IsDialogMessageW(hWndApp, &msg))
            {
				continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

void CBodyBasics::setParams()
{
	m_pRobot->setParams(m_pConfig);
}

void CBodyBasics::log(std::ofstream * pOfs, bool bHeader)
{
	if (pOfs == NULL)
		return;
	ConditionalLog(pOfs, "tO", m_pSyncSocket->m_tsOdroid, bHeader);
	ConditionalLog(pOfs, "tOW", m_pSyncSocket->m_tsWindows, bHeader);
	ConditionalLog(pOfs, "trigger", m_pSyncSocket->m_tsSquareWave, bHeader);
	//ConditionalLog(pOfs, "cntY", m_pSyncSocket->m_nPacketCount, bHeader);
	//ConditionalLog(pOfs, "cntN", m_pSyncSocket->m_nNoPacketCount, bHeader);
	ConditionalLog(pOfs, "tK", m_JointData.tsKinect, bHeader);
	ConditionalLog(pOfs, "tKW", m_JointData.tsWindows, bHeader);

	for (int i = 0; i < JOINT_DATA_SIZE; i++)
		ConditionalLog(pOfs, m_JointData.names[i].c_str(), m_JointData.data[i], bHeader);
	
	*pOfs << '\n';
}

void CBodyBasics::control()
{
	m_pRobot->updateState();
	pcRobotState pcState = m_pRobot->getState();
	INT64 tsWindows = GetTickCount64();

	float v, w, th;
	m_pRobot->calcControl(&v, &w, &th);

	if (!pcState->isFollowing /* || // if following is disabled
							  m_pRobot->isVisualCmdTooOld() */) // or if the visual command is not up-to-date
	{
		m_pRobot->setCmd(0.0, 0.0);
	}
	else
	{
		m_pRobot->setCmd(v, w);
	}
}

void CBodyBasics::calibrate()
{
	// Define the states of a finite state machine
	typedef enum _CalibState {
		Inactive = 0,
		CountDown,
		Act1,
		Completed,
		Aborted
	} CalibState;
	
	typedef struct _Move {
		int duration; // in milliseconds
		float params[4];
	} Move;
	typedef const Move * pcMove;

	typedef std::vector<pcMove> MoveSequence;

	static int iMove;
	static CalibState s_State = Inactive;
	static INT64 nTimeoutTick;
	INT64 nCurrentTick = GetTickCount64();
	if (s_State != Inactive)
	{
		if (m_pCalibKinectFile)
			log(m_pCalibKinectFile);
		else
			s_State = Aborted;
	}

	switch (s_State)
	{
	case Inactive:
	{
		if (m_pRobot->getState()->isCalibrating)
		{
			// --- Start Calibration ---

			// Create calib-Kinect file
			std::string fileNameKinect;
			generateFileName(fileNameKinect, "calib-Kinect");
			m_pCalibKinectFile = new std::ofstream(fileNameKinect, std::ofstream::out); // Open a calib file for writing
			log(m_pCalibKinectFile, true);
			
			m_pRobot->setCalibRobotLogging(true);

			// Let the robot stand still
			const Move m0 = { 0, 2.5f, (float)M_PI, 0.0f, 0.0f };
			m_pRobot->updateControlParams(m0.params);

			// State transition
			nTimeoutTick = nCurrentTick + 3000; // set timeout tick for CountDown
			s_State = CountDown;
		}		
		break;
	}

	case CountDown:
	{
		// Update status message
		const int nMaxCount = 64;
		TCHAR pszText[nMaxCount], pszPrevText[nMaxCount];
		StringCchPrintf(pszText, nMaxCount, L" Calibration: starting in %.0f seconds",
			ceil((nTimeoutTick - nCurrentTick) / 1000.0) );
		GetWindowText(m_hWndStatic, pszPrevText, nMaxCount);
		if (wcscmp(pszText, pszPrevText))
			SetWindowText(m_hWndStatic, pszText);

		// State transition
		if (nCurrentTick >= nTimeoutTick)
		{
			nTimeoutTick = nCurrentTick + 150000; // set timeout tick for Act1
			s_State = Act1;
			iMove = -1;

		}
		// Check if a request to stop calibration is received
		if (m_pRobot->getState()->isCalibrating == false) s_State = Aborted;
		break;
	}

	case Act1:
	{
		static INT64 nMoveUntilTick;
		const Move m1 = { 8000, 3.0f, (float)M_PI, -0.8f, 0.0f };
		const Move m2 = { 8000, 2.0f, (float)M_PI, -0.8f, 0.0f };
		const MoveSequence Moves = {&m1, &m2};
		const int nReps = 10;

		// Move initialization
		if (iMove == -1)
		{
			iMove = 0;
			nMoveUntilTick = nCurrentTick + Moves[iMove % Moves.size()]->duration;
		}
		
		// Execute the move
		m_pRobot->updateControlParams(Moves[iMove % Moves.size()]->params);

		// Update status message
		const int nMaxCount = 256;
		TCHAR pszText[nMaxCount], pszPrevText[nMaxCount];
		StringCchPrintf(pszText, nMaxCount, L" Calibration: Act1 in progress" \
			"\n\tMove%d out of %d Moves in progress" \
			"\n\t\tending in %.0f seconds." \
			"\nTiming out in %.0f seconds",
			iMove, Moves.size()*nReps, ceil((nMoveUntilTick - nCurrentTick) / 1000.0),
			ceil((nTimeoutTick - nCurrentTick) / 1000.0) );
		GetWindowText(m_hWndStatic, pszPrevText, nMaxCount);
		if (wcscmp(pszText, pszPrevText))
			SetWindowText(m_hWndStatic, pszText);

		// Move transition
		if (nCurrentTick >= nMoveUntilTick)
		{
			nMoveUntilTick = nCurrentTick + Moves[iMove % Moves.size()]->duration;
			iMove++;
		}

		// State transition
		if (iMove >= Moves.size() * nReps || nCurrentTick >= nTimeoutTick)
		{
			iMove = -1;
			nTimeoutTick += 3000;
			s_State = Completed;
		}
		// Check if a request to stop calibration is received
		if (m_pRobot->getState()->isCalibrating == false) s_State = Aborted;
		break;
	}
	
	case Completed:
	case Aborted:
	{
		// Close file
		if (m_pCalibKinectFile)
		{
			m_pCalibKinectFile->close();
			delete m_pCalibKinectFile;
			m_pCalibKinectFile = NULL;
		}

		m_pRobot->setCalibRobotLogging(false);

		// Update status message
		if (s_State == Completed)
			SetWindowText(m_hWndStatic, L" Calibration: Completed");
		else if (s_State == Aborted)
			SetWindowText(m_hWndStatic, L" Calibration: Aborted");
		
		if (m_pRobot->getState()->isCalibrating)
		{
			if (!m_pRobot->toggleCalibration())
				SetWindowText(m_hWndStatic, L"Unexpected error occurred!");
			else
				updateButtons();
		}
		
		// Recover control parameters
		m_pRobot->setParams(m_pConfig);

		// State transition
		s_State = Inactive;
		break;
	}
	}
}

void CBodyBasics::onPressingButtonFollow()
{
	if (!m_pRobot->toggleFollowing())
		SetWindowText(m_hWndStatic, L"Following cannot be turned on while calibration is in progress.");
	else
		updateButtons();
}

void CBodyBasics::onPressingButtonCalibrate()
{
	if (!m_pRobot->toggleCalibration())
		SetWindowText(m_hWndStatic, L"Calibration cannot start while following is on.");
	else
		updateButtons();	
}

void CBodyBasics::updateButtons()
{
	pcRobotState pcState = m_pRobot->getState();
	if (pcState->isCalibrating == true)
	{
		EnableWindow(m_hWndButtonFollow, false);
		EnableWindow(m_hWndButtonLoad, false);
		SetWindowText(m_hWndButtonCalibrate, L" Stop Calibration");
	}
	else
	{
		EnableWindow(m_hWndButtonFollow, true);
		EnableWindow(m_hWndButtonLoad, true);
		SetWindowText(m_hWndButtonCalibrate, L" Start Calibration");
	}

	if (pcState->isFollowing == true)
	{
		EnableWindow(m_hWndButtonCalibrate, false);
		SetWindowText(m_hWndButtonFollow, L" Stop Following");
	}
	else
	{
		EnableWindow(m_hWndButtonCalibrate, true);
		SetWindowText(m_hWndButtonFollow, L" Start Following");
	}
}

/// <summary>
/// Main processing function
/// </summary>
void CBodyBasics::Update()
{
    if (!m_pBodyFrameReader)
    {
        return;
    }

    IBodyFrame* pBodyFrame = NULL;

    HRESULT hr = m_pBodyFrameReader->AcquireLatestFrame(&pBodyFrame);

    if (SUCCEEDED(hr))
    {
        INT64 nTime = 0;

        hr = pBodyFrame->get_RelativeTime(&nTime);

        IBody* ppBodies[BODY_COUNT] = {0};

        if (SUCCEEDED(hr))
        {
            hr = pBodyFrame->GetAndRefreshBodyData(_countof(ppBodies), ppBodies);
        }

        if (SUCCEEDED(hr))
        {
            ProcessBody(nTime, BODY_COUNT, ppBodies);
        }

        for (int i = 0; i < _countof(ppBodies); ++i)
        {
            SafeRelease(ppBodies[i]);
        }
    }

    SafeRelease(pBodyFrame);
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBodyBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CBodyBasics* pThis = NULL;

    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CBodyBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CBodyBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }
	
    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBodyBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
        case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Get and initialize the default Kinect sensor
            InitializeDefaultSensor();

			// Create a ROS publisher
			//m_pRosPublisher = new RosPublisher;

			//
			if (!m_pSyncSocket->init(hWnd)) break;
			if (!m_pRobot->init(hWnd)) break;

			SetFocus(hWnd);
        }
        break;

        // If the titlebar X is clicked, destroy app
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            // Quit the main message pump
            PostQuitMessage(0);
            break;

		case WM_HOTKEY:
		{
			switch (wParam)
			{
			case 1:
				onPressingButtonFollow();
				break;
			case 2:
				onPressingButtonCalibrate();
				break;
			}
			break;
		}
		case WM_COMMAND:
			switch (wParam)
			{
			case BN_CLICKED: //WM_COMMAND
				HWND hButton = (HWND)lParam;
				if (m_hWndButtonFollow == hButton)
				{
					onPressingButtonFollow();
				}
				else if (m_hWndButtonCalibrate == hButton)
				{
					onPressingButtonCalibrate();
				}
				else if (m_hWndButtonOpenConfig == hButton)
				{
					// Open Config Button
					system("notepad.exe config.txt");
				}
				else if (m_hWndButtonLoad == hButton)
				{
					// Load button
					m_pConfig->load();
					m_pConfig->resetCounter();
					setParams();
					int cnt = m_pConfig->getUpdateCount();
					TCHAR pszText[32];
					StringCchPrintf(pszText, 32, L"%d parameter%s updated.", cnt, cnt > 1 ? L"s" : L"");
					SetWindowText(m_hWndStatic, pszText);
				}
				break;
			}
			break;
		default:
			break;
    }

    return FALSE;
}

/// <summary>
/// Initializes the default Kinect sensor
/// </summary>
/// <returns>indicates success or failure</returns>
HRESULT CBodyBasics::InitializeDefaultSensor()
{
    HRESULT hr;

    hr = GetDefaultKinectSensor(&m_pKinectSensor);
    if (FAILED(hr))
    {
        return hr;
    }

    if (m_pKinectSensor)
    {
        // Initialize the Kinect and get coordinate mapper and the body reader
        IBodyFrameSource* pBodyFrameSource = NULL;

        hr = m_pKinectSensor->Open();

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
        }

        if (SUCCEEDED(hr))
        {
            hr = m_pKinectSensor->get_BodyFrameSource(&pBodyFrameSource);
        }

        if (SUCCEEDED(hr))
        {
            hr = pBodyFrameSource->OpenReader(&m_pBodyFrameReader);
        }

        SafeRelease(pBodyFrameSource);
    }

    if (!m_pKinectSensor || FAILED(hr))
    {
        SetStatusMessage(L"No ready Kinect found!", 10000, true);
        return E_FAIL;
    }

    return hr;
}

/// <summary>
/// Handle new body data
/// <param name="nTime">timestamp of frame</param>
/// <param name="nBodyCount">body data count</param>
/// <param name="ppBodies">body data in frame</param>
/// </summary>
void CBodyBasics::ProcessBody(INT64 nTime, int nBodyCount, IBody** ppBodies)
{
	int iClosest = -1; // the index of the body closest to the camera
	float dSqrMin = 25.0; // squared x-z-distance of the closest body
	INT64 t0Windows = GetTickCount64();


    if (m_hWnd)
    {
        HRESULT hr = EnsureDirect2DResources();

        if (SUCCEEDED(hr) && m_pRenderTarget && m_pCoordinateMapper)
        {
            m_pRenderTarget->BeginDraw();
            m_pRenderTarget->Clear();

            RECT rct;
            GetClientRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rct);
            int width = rct.right;
            int height = rct.bottom;

            for (int i = 0; i < nBodyCount; ++i)
            {
                IBody* pBody = ppBodies[i];
                if (pBody)
                {
                    BOOLEAN bTracked = false;
                    hr = pBody->get_IsTracked(&bTracked);

                    if (SUCCEEDED(hr) && bTracked)
                    {
                        Joint joints[JointType_Count];
                        D2D1_POINT_2F jointPoints[JointType_Count];
                        HandState leftHandState = HandState_Unknown;
                        HandState rightHandState = HandState_Unknown;

                        pBody->get_HandLeftState(&leftHandState);
                        pBody->get_HandRightState(&rightHandState);

                        hr = pBody->GetJoints(_countof(joints), joints);
                        if (SUCCEEDED(hr))
                        {
                            for (int j = 0; j < _countof(joints); ++j)
                            {
                                jointPoints[j] = BodyToScreen(joints[j].Position, width, height);
                            }

                            DrawBody(joints, jointPoints);

                            DrawHand(leftHandState, jointPoints[JointType_HandLeft]);
                            DrawHand(rightHandState, jointPoints[JointType_HandRight]);

							// Find the closest body, if any.
							float px = joints[JointType_SpineBase].Position.X;
							float pz = joints[JointType_SpineBase].Position.Z;
							float dSqr = px * px + pz * pz;
							if (dSqrMin > dSqr)
							{
								dSqrMin = dSqr;
								iClosest = i;
							}
						}
                    }
                }
            }

            hr = m_pRenderTarget->EndDraw();

            // Device lost, need to recreate the render target
            // We'll dispose it now and retry drawing
            if (D2DERR_RECREATE_TARGET == hr)
            {
                hr = S_OK;
                DiscardDirect2DResources();
            }
        }

		if (!m_nStartTime)
		{
			m_nStartTime = nTime;
		}
		
        double fps = 0.0;

        LARGE_INTEGER qpcNow = {0};
        if (m_fFreq)
        {
            if (QueryPerformanceCounter(&qpcNow))
            {
                if (m_nLastCounter)
                {
                    m_nFramesSinceUpdate++;
                    fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
                }
            }
        }

		

		// Process the closest body, if any.
		if (iClosest > -1)
		{
			Joint joints[JointType_Count];
			ppBodies[iClosest]->GetJoints(_countof(joints), joints);

			float pxSum = 0, pzSum = 0;
			int cnt = 0;
			for (int j = 0; j < JointType_Count; j++)
			{
				if (joints[j].TrackingState == TrackingState_Tracked)
				{
					float px = joints[j].Position.X;
					//float py = joints[JointType_SpineBase].Position.Y;
					float pz = joints[j].Position.Z;

					pxSum += px;
					pzSum += pz;
					cnt++;
				}
			}
			
			if (joints[JointType_HandTipLeft].Position.Y > 0.6 && joints[JointType_HandTipRight].Position.Y > 0.6)
			{
				// Stop
				m_pRobot->updateVisualCmd(0, 0);
			}
			else if(joints[JointType_HandTipLeft].Position.Y > 0.6)
			{
				// Adjust Heading
				m_pRobot->updateVisualCmd(joints[JointType_HandTipLeft].Position.X - pxSum / cnt, pzSum / cnt);
			}
			else if (joints[JointType_HandTipRight].Position.Y > 0.6)
			{
				// Adjust Heading
				m_pRobot->updateVisualCmd(joints[JointType_HandTipRight].Position.X - pxSum / cnt, pzSum / cnt);
			}
			else
			{
				//m_pRobot->updateVisualCmd(pxSum / cnt, pzSum / cnt);
				m_pRobot->updateVisualCmd(0, pzSum / cnt);
			}
			
			// Get data ready for recording
			m_JointData.tsWindows = GetTickCount64(); // For now not to be logged.
			m_JointData.tsKinect = (nTime - m_nStartTime) / 10000;
			// Write joint states to the struct
			int i = 0;
			for (auto const &jt : jointTypeMap)
			{
				m_JointData.data[i++] = joints[jt.first].Position.X;
				m_JointData.data[i++] = joints[jt.first].Position.Y;
				m_JointData.data[i++] = joints[jt.first].Position.Z;
			}

			// Record data if following is on
			// if (m_pRobot->getState()->isFollowing == true)

			
			log(m_pKinectFile);
			
			calibrate();
				
		}
		else
		{
			//m_pRobot->setCmd(cmd[0], cmd[1]); // debug
		}

		//m_pRosPublisher->publish(cmd);

		WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage),
			L" FPS = %0.2f; Time = %.0f s", fps, (nTime - m_nStartTime) / 1.0e7);

		if (SetStatusMessage(szStatusMessage, 500, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFramesSinceUpdate = 0;
		}
		
		
    }

	
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
/// <param name="showTimeMsec">time in milliseconds to ignore future status messages</param>
/// <param name="bForce">force status update</param>
bool CBodyBasics::SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce)
{
    INT64 now = GetTickCount64();

    if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
    {
        SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
        m_nNextStatusTime = now + nShowTimeMsec;

        return true;
    }

    return false;
}

/// <summary>
/// Ensure necessary Direct2d resources are created
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CBodyBasics::EnsureDirect2DResources()
{
    HRESULT hr = S_OK;

    if (m_pD2DFactory && !m_pRenderTarget)
    {
        RECT rc;
        GetWindowRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rc);  

        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        D2D1_SIZE_U size = D2D1::SizeU(width, height);
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        rtProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

        // Create a Hwnd render target, in order to render to the window set in initialize
        hr = m_pD2DFactory->CreateHwndRenderTarget(
            rtProps,
            D2D1::HwndRenderTargetProperties(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), size),
            &m_pRenderTarget
        );

        if (FAILED(hr))
        {
            SetStatusMessage(L"Couldn't create Direct2D render target!", 10000, true);
            return hr;
        }

        // light green
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.27f, 0.75f, 0.27f), &m_pBrushJointTracked);

        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &m_pBrushJointInferred);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 1.0f), &m_pBrushBoneTracked);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray, 1.0f), &m_pBrushBoneInferred);

        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 0.5f), &m_pBrushHandClosed);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 0.5f), &m_pBrushHandOpen);
        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue, 0.5f), &m_pBrushHandLasso);
    }

    return hr;
}

/// <summary>
/// Dispose Direct2d resources 
/// </summary>
void CBodyBasics::DiscardDirect2DResources()
{
    SafeRelease(m_pRenderTarget);

    SafeRelease(m_pBrushJointTracked);
    SafeRelease(m_pBrushJointInferred);
    SafeRelease(m_pBrushBoneTracked);
    SafeRelease(m_pBrushBoneInferred);

    SafeRelease(m_pBrushHandClosed);
    SafeRelease(m_pBrushHandOpen);
    SafeRelease(m_pBrushHandLasso);
}

/// <summary>
/// Converts a body point to screen space
/// </summary>
/// <param name="bodyPoint">body point to tranform</param>
/// <param name="width">width (in pixels) of output buffer</param>
/// <param name="height">height (in pixels) of output buffer</param>
/// <returns>point in screen-space</returns>
D2D1_POINT_2F CBodyBasics::BodyToScreen(const CameraSpacePoint& bodyPoint, int width, int height)
{
    // Calculate the body's position on the screen
    DepthSpacePoint depthPoint = {0};
    m_pCoordinateMapper->MapCameraPointToDepthSpace(bodyPoint, &depthPoint);

    float screenPointX = static_cast<float>(depthPoint.X * width) / cDepthWidth;
    float screenPointY = static_cast<float>(depthPoint.Y * height) / cDepthHeight;

    return D2D1::Point2F(screenPointX, screenPointY);
}

/// <summary>
/// Draws a body 
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
void CBodyBasics::DrawBody(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints)
{
    // Draw the bones

    // Torso
    DrawBone(pJoints, pJointPoints, JointType_Head, JointType_Neck);
    DrawBone(pJoints, pJointPoints, JointType_Neck, JointType_SpineShoulder);
    DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_SpineMid);
    DrawBone(pJoints, pJointPoints, JointType_SpineMid, JointType_SpineBase);
    DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderRight);
    DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderLeft);
    DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipRight);
    DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipLeft);
    
    // Right Arm    
    DrawBone(pJoints, pJointPoints, JointType_ShoulderRight, JointType_ElbowRight);
    DrawBone(pJoints, pJointPoints, JointType_ElbowRight, JointType_WristRight);
    DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_HandRight);
    DrawBone(pJoints, pJointPoints, JointType_HandRight, JointType_HandTipRight);
    DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_ThumbRight);

    // Left Arm
    DrawBone(pJoints, pJointPoints, JointType_ShoulderLeft, JointType_ElbowLeft);
    DrawBone(pJoints, pJointPoints, JointType_ElbowLeft, JointType_WristLeft);
    DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_HandLeft);
    DrawBone(pJoints, pJointPoints, JointType_HandLeft, JointType_HandTipLeft);
    DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_ThumbLeft);

    // Right Leg
    DrawBone(pJoints, pJointPoints, JointType_HipRight, JointType_KneeRight);
    DrawBone(pJoints, pJointPoints, JointType_KneeRight, JointType_AnkleRight);
    DrawBone(pJoints, pJointPoints, JointType_AnkleRight, JointType_FootRight);

    // Left Leg
    DrawBone(pJoints, pJointPoints, JointType_HipLeft, JointType_KneeLeft);
    DrawBone(pJoints, pJointPoints, JointType_KneeLeft, JointType_AnkleLeft);
    DrawBone(pJoints, pJointPoints, JointType_AnkleLeft, JointType_FootLeft);

    // Draw the joints
    for (int i = 0; i < JointType_Count; ++i)
    {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(pJointPoints[i], c_JointThickness, c_JointThickness);

        if (pJoints[i].TrackingState == TrackingState_Inferred)
        {
            m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointInferred);
        }
        else if (pJoints[i].TrackingState == TrackingState_Tracked)
        {
            m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointTracked);
        }
    }
}

/// <summary>
/// Draws one bone of a body (joint to joint)
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="joint0">one joint of the bone to draw</param>
/// <param name="joint1">other joint of the bone to draw</param>
void CBodyBasics::DrawBone(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints, JointType joint0, JointType joint1)
{
    TrackingState joint0State = pJoints[joint0].TrackingState;
    TrackingState joint1State = pJoints[joint1].TrackingState;

    // If we can't find either of these joints, exit
    if ((joint0State == TrackingState_NotTracked) || (joint1State == TrackingState_NotTracked))
    {
        return;
    }

    // Don't draw if both points are inferred
    if ((joint0State == TrackingState_Inferred) && (joint1State == TrackingState_Inferred))
    {
        return;
    }

    // We assume all drawn bones are inferred unless BOTH joints are tracked
    if ((joint0State == TrackingState_Tracked) && (joint1State == TrackingState_Tracked))
    {
        m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneTracked, c_TrackedBoneThickness);
    }
    else
    {
        m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneInferred, c_InferredBoneThickness);
    }
}

/// <summary>
/// Draws a hand symbol if the hand is tracked: red circle = closed, green circle = opened; blue circle = lasso
/// </summary>
/// <param name="handState">state of the hand</param>
/// <param name="handPosition">position of the hand</param>
void CBodyBasics::DrawHand(HandState handState, const D2D1_POINT_2F& handPosition)
{
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(handPosition, c_HandSize, c_HandSize);

    switch (handState)
    {
        case HandState_Closed:
            m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandClosed);
            break;

        case HandState_Open:
            m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandOpen);
            break;

        case HandState_Lasso:
            m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandLasso);
            break;
    }
}
