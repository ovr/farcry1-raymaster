//////////////////////////////////////////////////////////////////////
//
//	File: DemoStress.cpp
//	Description: Many objects and lights: a static object of the game
//	drawn many times, lit by moving point lights, under a camera flying
//	round; the demo logs the frame times.
//
//	The instances stand in a square grid on z = 0, the cells 1.5 times
//	the size of the object, each turned about z by its own fixed angle,
//	and are drawn in one scene (EF_StartEf, IStatObj::Render with a
//	matrix per instance and FOB_TRANS_MASK, EF_EndEf3D) with a dark
//	ambient colour. The point lights, each of its own colour and with a
//	radius of 4 cells, go round circles over the grid at their own speeds;
//	yellow crosses mark them. The camera goes round the middle of the grid
//	by 1.5 degrees per frame, 0.6 times the grid's width away and 0.45
//	times above it, looking at the middle, from -y at frame 1 towards +x:
//	frames 100 and 200 look from about +y and about -x.
//	The frame time (ITimer::GetAsyncCurTime from one AfterFrame to the
//	next, after the demo's own log lines) is logged as average, minimum
//	and maximum every 50 frames and
//	for the whole run at its end, without the first frames (warm-up) and
//	without the frames the host saved to files (reading the back buffer
//	back takes longer than the frame).
//
//	Scene file keys (Demos\DemoStress.ini):
//	  object = <file>     the object (default: the first .cgf under objects/)
//	  count = <n>         the number of instances (default 400)
//	  lights = <n>        the number of point lights (default 6, 1 to 16)
//	  warmup = <frames>   the frames left out of the frame times (default 10)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <algorithm>

class CDemoStress : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoStress"; }
  virtual const char *GetDefaultFrames() const { return "1,100,200"; }

  virtual bool Init(CDemoHost &Host)
  {
    m_pObj = Host.LoadObject("object");
    if (!m_pObj)
      return false;
    const int nCount = min(max(Host.GetScene().GetInt("count", 400), 1), 100000);
    m_nLights = min(max(Host.GetScene().GetInt("lights", 6), 1), 16);
    m_nWarmup = max(Host.GetScene().GetInt("warmup", 10), 0);
    m_Saved = SavedFrames();

    // The grid: nSide x nSide cells, filled row by row from -x -y.
    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    m_fCell = max(max(vMax.x - vMin.x, vMax.y - vMin.y), 0.1f) * 1.5f;
    m_fTop = max(vMax.z, 0.1f);
    const int nSide = (int)ceilf(sqrtf((float)nCount));
    m_fWidth = nSide * m_fCell;
    m_Mats.resize(nCount);
    m_Pos.resize(nCount);
    for (int i = 0; i < nCount; i++)
    {
      m_Pos[i] = Vec3(((i % nSide) - (nSide - 1) * 0.5f) * m_fCell, ((i / nSide) - (nSide - 1) * 0.5f) * m_fCell, 0);
      // Matrix44 takes a row vector (v * M): the turned axes in the rows, the position in the last row.
      const float fTurn = DEG2RAD((float)((i * 37) % 360));
      Matrix44 &Mat = m_Mats[i];
      Mat.SetIdentity();
      float *m = Mat.GetData();
      m[0] = cosf(fTurn);
      m[1] = sinf(fTurn);
      m[4] = -sinf(fTurn);
      m[5] = cosf(fTurn);
      Mat.SetTranslationOLD(m_Pos[i]);
    }
    Host.Log("DemoStress: %d instances in %d x %d cells of %g m, %d lights, r_VSync %s", nCount, nSide, nSide, m_fCell, m_nLights,
      Host.GetCVarString("r_VSync").c_str());
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const float fAround = DEG2RAD(1.5f * (nFrame - 1));
    const Vec3 vEye(sinf(fAround) * m_fWidth * 0.6f, -cosf(fAround) * m_fWidth * 0.6f, m_fWidth * 0.45f + m_fTop);
    // As Host.LookAt, with the far plane beyond the farthest corner of the grid.
    CCamera Cam;
    Cam.Init(pRenderer->GetWidth(), pRenderer->GetHeight(), DEG2RAD(60.0f), max(m_fWidth * 1.5f + m_fTop * 2, DEFAULT_ZMAX));
    Cam.SetPos(vEye);
    Cam.SetAngle(ConvertVectorToCameraAngles(-vEye));
    Cam.Update();

    static const float Colours[8][3] = { { 1, 0.3f, 0.3f }, { 0.3f, 1, 0.3f }, { 0.3f, 0.3f, 1 }, { 1, 1, 0.3f },
      { 1, 0.3f, 1 }, { 0.3f, 1, 1 }, { 1, 0.6f, 0.2f }, { 0.6f, 0.2f, 1 } };
    CDLight Lights[16];
    for (int i = 0; i < m_nLights; i++)
    {
      // Circles from a tenth to a half of the grid's width, every other one the other way round.
      const float fRadius = m_fWidth * (0.1f + 0.4f * i / max(m_nLights - 1, 1));
      const float fAngle = DEG2RAD((2.0f + i) * (i % 2 ? -1 : 1) * (nFrame - 1) + 360.0f * i / m_nLights);
      CDLight &Light = Lights[i];
      Light.m_Flags = DLF_POINT;
      Light.m_Origin = Vec3(cosf(fAngle) * fRadius, sinf(fAngle) * fRadius, m_fTop * 1.5f);
      Light.m_fRadius = Light.m_fEndRadius = m_fCell * 4;
      Light.m_Color = CFColor(Colours[i % 8][0], Colours[i % 8][1], Colours[i % 8][2], 1);
    }

    SRendParams Scene;
    Host.BeginScene(Cam, Scene, Lights, m_nLights);
    if (nFrame == 1)
    {
      char szIds[128] = "";
      for (int i = 0; i < m_nLights; i++)
        sprintf(szIds + strlen(szIds), " %d", Lights[i].m_Id);
      Host.Log("DemoStress: the renderer's light ids%s, light mask 0x%x", szIds, Scene.nDLightMask);
    }
    for (size_t i = 0; i < m_Mats.size(); i++)
    {
      SRendParams Params;
      Params.pMatrix = &m_Mats[i];
      Params.dwFObjFlags = FOB_TRANS_MASK;
      Params.nDLightMask = Scene.nDLightMask;
      Params.vAmbientColor = Vec3(0.15f, 0.15f, 0.15f);
      Params.fDistance = GetLength(vEye - m_Pos[i]);
      m_pObj->Render(Params, Vec3(0, 0, 0), 0);
    }
    Host.EndScene();

    // The lights' places.
    const CFColor Yellow(1, 1, 0, 1);
    const float d = m_fCell * 0.2f;
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->SetWhiteTexture();
    for (int i = 0; i < m_nLights; i++)
    {
      const Vec3 &P = Lights[i].m_Origin;
      pRenderer->DrawLineColor(P - Vec3(d, 0, 0), Yellow, P + Vec3(d, 0, 0), Yellow);
      pRenderer->DrawLineColor(P - Vec3(0, d, 0), Yellow, P + Vec3(0, d, 0), Yellow);
      pRenderer->DrawLineColor(P - Vec3(0, 0, d), Yellow, P + Vec3(0, 0, d), Yellow);
    }
    pRenderer->SetState(GS_DEPTHWRITE);
  }

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    const float fNow = Host.GetSystem()->GetITimer()->GetAsyncCurTime();
    const bool bSaved = std::binary_search(m_Saved.begin(), m_Saved.end(), nFrame);
    if (nFrame > m_nWarmup && m_fLast > 0 && !bSaved)
    {
      const float fTime = fNow - m_fLast;
      m_Run.Add(fTime, nFrame);
      m_Part.Add(fTime, nFrame);
    }
    if (nFrame % 50 == 0)
    {
      LogTimes(Host, m_Part, "frames to %d", nFrame);
      m_Part = STimes();
    }
    // The next frame time starts after the log line.
    m_fLast = Host.GetSystem()->GetITimer()->GetAsyncCurTime();
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    LogTimes(Host, m_Run, "the run (%d frames)", m_Run.nFrames);
    if (m_pObj)
      Host.Get3DEngine()->ReleaseObject(m_pObj);
    m_pObj = NULL;
  }

private:
  //! Frame times in seconds.
  struct STimes
  {
    int nFrames = 0;
    double fSum = 0;
    float fMin = 0, fMax = 0;
    int nMaxFrame = 0;

    void Add(float fTime, int nFrame)
    {
      if (!nFrames || fTime < fMin)
        fMin = fTime;
      if (!nFrames || fTime > fMax)
      {
        fMax = fTime;
        nMaxFrame = nFrame;
      }
      fSum += fTime;
      nFrames++;
    }
  };

  static void LogTimes(CDemoHost &Host, const STimes &Times, const char *szWhat, int nValue)
  {
    char szWhatLine[64];
    _snprintf(szWhatLine, sizeof(szWhatLine) - 1, szWhat, nValue);
    szWhatLine[sizeof(szWhatLine) - 1] = 0;
    if (!Times.nFrames)
    {
      Host.Log("DemoStress: frame time of %s: no frames", szWhatLine);
      return;
    }
    const double fAverage = Times.fSum / Times.nFrames;
    Host.Log("DemoStress: frame time of %s: average %.2f ms (%.1f per second), min %.2f ms, max %.2f ms (frame %d), %d frames",
      szWhatLine, fAverage * 1000, fAverage > 0 ? 1 / fAverage : 0.0, Times.fMin * 1000.0f, Times.fMax * 1000.0f, Times.nMaxFrame,
      Times.nFrames);
  }

  //! The frames the host saves: --frames of the demo's arguments (XRENDERTRACED_DEMO_ARGS, as the host
  //! reads them), or else the default ones.
  std::vector<int> SavedFrames() const
  {
    char szArgs[1024] = "";
    const DWORD nLen = GetEnvironmentVariableA("XRENDERTRACED_DEMO_ARGS", szArgs, sizeof(szArgs));
    if (nLen >= sizeof(szArgs))
      szArgs[0] = 0;
    const char *szFrames = GetDefaultFrames();
    std::string Args = szArgs, Frames;
    const size_t nOption = Args.find("--frames ");
    if (nOption != std::string::npos)
    {
      const size_t nBegin = Args.find_first_not_of(" \"", nOption + 9);
      const size_t nEnd = nBegin == std::string::npos ? std::string::npos : Args.find_first_of(" \"", nBegin);
      if (nBegin != std::string::npos)
        Frames = Args.substr(nBegin, nEnd == std::string::npos ? std::string::npos : nEnd - nBegin);
      szFrames = Frames.c_str();
    }
    std::vector<int> List;
    for (const char *p = szFrames; *p;)
    {
      char *pEnd = NULL;
      const long n = strtol(p, &pEnd, 10);
      if (pEnd == p)
      {
        p++;
        continue;
      }
      List.push_back((int)n);
      p = pEnd;
    }
    std::sort(List.begin(), List.end());
    return List;
  }

  IStatObj *m_pObj = NULL;
  //! The placement and the position of each instance; the matrices stay until the scene is drawn.
  std::vector<Matrix44> m_Mats;
  std::vector<Vec3> m_Pos;
  float m_fCell = 1, m_fTop = 1, m_fWidth = 1;
  int m_nLights = 6;
  int m_nWarmup = 10;
  std::vector<int> m_Saved;
  float m_fLast = 0;
  STimes m_Run, m_Part;
};

DEMO_PLUGIN(CDemoStress)
