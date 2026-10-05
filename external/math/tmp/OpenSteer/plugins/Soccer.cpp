
#include <iomanip>
#include <sstream>
#include "OpenSteer/SimpleVehicle.h"
#include "OpenSteer/OpenSteerDemo.h"
#include "OpenSteer/Draw.h"
#include "OpenSteer/Color.h"
#include "OpenSteer/UnusedParameter.h"

namespace {

    using namespace OpenSteer;

    Vec3 playerPosition[9] = {
        Vec3(4,0,0),
        Vec3(7,0,-5),
        Vec3(7,0,5),
        Vec3(10,0,-3),
        Vec3(10,0,3),
        Vec3(15,0, -8),
        Vec3(15,0,0),
        Vec3(15,0,8),
        Vec3(4,0,0)
    };

    class AABBox{
    public:
        AABBox(Vec3 &min, Vec3& max): m_min(min), m_max(max){}
        AABBox(Vec3 min, Vec3 max): m_min(min), m_max(max){}
        bool	InsideX(const Vec3 p){if(p.x < m_min.x || p.x > m_max.x)	return false;return true;}
        bool	InsideZ(const Vec3 p){if(p.z < m_min.z || p.z > m_max.z)	return false;return true;}
        void	draw(){
            Vec3 b,c;
            b = Vec3(m_min.x, 0, m_max.z);
            c = Vec3(m_max.x, 0, m_min.z);
            Color color(1.0f,1.0f,0.0f);
            drawLineAlpha(m_min, b, color, 1.0f);
            drawLineAlpha(b, m_max, color, 1.0f);
            drawLineAlpha(m_max, c, color, 1.0f);
            drawLineAlpha(c,m_min, color, 1.0f);
        }
    private:
        Vec3 m_min;
        Vec3 m_max;
    };

    class Ball : public SimpleVehicle{
    public:
        Ball(AABBox *bbox) : m_bbox(bbox) {reset();}

        void reset (void)
        {
            SimpleVehicle::reset (); 
            setSpeed (0.0f);         
            setMaxForce (9.0f);      
            setMaxSpeed (9.0f);         

            setPosition(0,0,0);
            clearTrailHistory ();    
            setTrailParameters (100, 6000);
        }

        void update (const float currentTime, const float elapsedTime)
        {
            applyBrakingForce(1.5f, elapsedTime);
            applySteeringForce(velocity(), elapsedTime);

            if(!m_bbox->InsideX(position()))
            {
                Vec3 d = velocity();
                regenerateOrthonormalBasis(Vec3(-d.x, d.y, d.z));
                applySteeringForce(velocity(), elapsedTime);
            }
            if(!m_bbox->InsideZ(position()))
            {
                Vec3 d = velocity();
                regenerateOrthonormalBasis(Vec3(d.x, d.y, -d.z));
                applySteeringForce(velocity(), elapsedTime);
            }
        recordTrailVertex (currentTime, position());
        }

        void kick(Vec3 dir, const float elapsedTime){
            OPENSTEER_UNUSED_PARAMETER(elapsedTime);

            setSpeed(dir.length());
            regenerateOrthonormalBasis(dir);
        }

        void draw (void)
        {
            drawBasic2dCircularVehicle (this, Color(0.0f,1.0f,0.0f));
            drawTrail ();
        }

        AABBox *m_bbox;
    };

    class Player : public SimpleVehicle
    {
    public:

        Player (std::vector<Player*> others, std::vector<Player*> allplayers, Ball* ball, bool isTeamA, int id) : m_others(others), m_AllPlayers(allplayers), m_Ball(ball), b_ImTeamA(isTeamA), m_MyID(id) {reset ();}

        void reset (void)
        {
            SimpleVehicle::reset (); 
            setSpeed (0.0f);         
            setMaxForce (3000.7f);      
            setMaxSpeed (10);         

            setPosition(b_ImTeamA ? frandom01()*20 : -frandom01()*20, 0, (frandom01()-0.5f)*20);
            if(m_MyID < 9)
                {
                if(b_ImTeamA)
                    setPosition(playerPosition[m_MyID]);
                else
                    setPosition(Vec3(-playerPosition[m_MyID].x, playerPosition[m_MyID].y, playerPosition[m_MyID].z));
                }
            m_home = position();
            clearTrailHistory ();    
            setTrailParameters (10, 60);
        }

        void update (const float , const float elapsedTime)
        {

            const float distToBall = Vec3::distance (position(), m_Ball->position());
            const float sumOfRadii = radius() + m_Ball->radius();
            if (distToBall < sumOfRadii)
                m_Ball->kick((m_Ball->position()-position())*50, elapsedTime);

            Vec3 collisionAvoidance = steerToAvoidNeighbors(1, (AVGroup&)m_AllPlayers);
            if(collisionAvoidance != Vec3::zero)
                applySteeringForce (collisionAvoidance, elapsedTime);
            else
                {
                float distHomeToBall = Vec3::distance (m_home, m_Ball->position());
                if( distHomeToBall < 12.0f)
                    {

                        if( b_ImTeamA ? position().x > m_Ball->position().x : position().x < m_Ball->position().x)
                        {
                        Vec3 seekTarget = xxxsteerForSeek(m_Ball->position());
                        applySteeringForce (seekTarget, elapsedTime);
                        }
                    else
                        {
                        if( distHomeToBall < 12.0f)
                            {
                            float Z = m_Ball->position().z - position().z > 0 ? -1.0f : 1.0f;
                            Vec3 behindBall = m_Ball->position() + (b_ImTeamA ? Vec3(2.0f,0.0f,Z) : Vec3(-2.0f,0.0f,Z));
                            Vec3 behindBallForce = xxxsteerForSeek(behindBall);
                            annotationLine (position(), behindBall , Color(0.0f,1.0f,0.0f));
                            Vec3 evadeTarget = xxxsteerForFlee(m_Ball->position());
                            applySteeringForce (behindBallForce*10.0f + evadeTarget, elapsedTime);
                            }
                        }
                    }
                else	
                    {
                    Vec3 seekTarget = xxxsteerForSeek(m_home);
                    Vec3 seekHome = xxxsteerForSeek(m_home);
                    applySteeringForce (seekTarget+seekHome, elapsedTime);
                    }

                }
        }

        void draw ()
        {
            drawBasic2dCircularVehicle (this, b_ImTeamA ? Color(1.0f,0.0f,0.0f):Color(0.0f,0.0f,1.0f));
            drawTrail ();
        }

        const std::vector<Player*>	m_others;
        const std::vector<Player*>	m_AllPlayers;
        Ball*	m_Ball;
        bool	b_ImTeamA;
        int		m_MyID;
        Vec3		m_home;
    };

    class MicTestPlugIn : public PlugIn
    {
    public:

        const char* name (void) {return "Michael's Simple Soccer";}

        virtual ~MicTestPlugIn() {}

        void open (void)
        {

            m_bbox = new AABBox(Vec3(-20,0,-10), Vec3(20,0,10));

            m_TeamAGoal = new AABBox(Vec3(-21,0,-7), Vec3(-19,0,7));

            m_TeamBGoal = new AABBox(Vec3(19,0,-7), Vec3(21,0,7));

            m_Ball = new Ball(m_bbox);

            m_PlayerCountA = 8;
            for(unsigned int i=0; i < m_PlayerCountA ; i++)
            {
                Player *pMicTest = new Player(TeamA, m_AllPlayers, m_Ball, true, i);
                OpenSteerDemo::selectedVehicle = pMicTest;
                TeamA.push_back (pMicTest);
                m_AllPlayers.push_back(pMicTest);
            }

            m_PlayerCountB = 8;
            for(unsigned int i=0; i < m_PlayerCountB ; i++)
            {
                Player *pMicTest = new Player(TeamB, m_AllPlayers, m_Ball, false, i);
                OpenSteerDemo::selectedVehicle = pMicTest;
                TeamB.push_back (pMicTest);
                m_AllPlayers.push_back(pMicTest);
            }

            OpenSteerDemo::init2dCamera (*m_Ball);
            OpenSteerDemo::camera.setPosition (10, OpenSteerDemo::camera2dElevation, 10);
            OpenSteerDemo::camera.fixedPosition.set (40, 40, 40);
            OpenSteerDemo::camera.mode = Camera::cmFixed;
            m_redScore = 0;
            m_blueScore = 0;
        }

        void update (const float currentTime, const float elapsedTime)
        {

            for(unsigned int i=0; i < m_PlayerCountA ; i++)
                TeamA[i]->update (currentTime, elapsedTime);
            for(unsigned int i=0; i < m_PlayerCountB ; i++)
                TeamB[i]->update (currentTime, elapsedTime);
            m_Ball->update(currentTime, elapsedTime);

            if(m_TeamAGoal->InsideX(m_Ball->position()) && m_TeamAGoal->InsideZ(m_Ball->position()))
            {
                m_Ball->reset();	
                m_redScore++;
            }
            if(m_TeamBGoal->InsideX(m_Ball->position()) && m_TeamBGoal->InsideZ(m_Ball->position()))
            {
                m_Ball->reset();	
                    m_blueScore++;
            }

        }

        void redraw (const float currentTime, const float elapsedTime)
        {

            OpenSteerDemo::gridUtility (Vec3(0,0,0));

            for(unsigned int i=0; i < m_PlayerCountA ; i++)
                TeamA[i]->draw ();
            for(unsigned int i=0; i < m_PlayerCountB ; i++)
                TeamB[i]->draw ();
            m_Ball->draw();
            m_bbox->draw();
            m_TeamAGoal->draw();
            m_TeamBGoal->draw();
            {
                std::ostringstream annote;
                annote << "Red: "<< m_redScore;
                draw2dTextAt3dLocation (annote, Vec3(23,0,0), Color(1.0f,0.7f,0.7f), drawGetWindowWidth(), drawGetWindowHeight());
            }
            {
                std::ostringstream annote;
                annote << "Blue: "<< m_blueScore;
                draw2dTextAt3dLocation (annote, Vec3(-23,0,0), Color(0.7f,0.7f,1.0f), drawGetWindowWidth(), drawGetWindowHeight());
            }

    if(0)
        for(unsigned int i=0; i < m_PlayerCountA ; i++)
            {
                std::ostringstream annote;
                annote << std::setprecision (2) << std::setiosflags (std::ios::fixed);
                annote << "      speed: " << TeamA[i]->speed() << "ID:" << i << std::ends;
                draw2dTextAt3dLocation (annote, TeamA[i]->position(), gRed, drawGetWindowWidth(), drawGetWindowHeight());
                draw2dTextAt3dLocation (*"start", Vec3::zero, gGreen, drawGetWindowWidth(), drawGetWindowHeight());
            }

            OpenSteerDemo::updateCamera (currentTime, elapsedTime, OpenSteerDemo::selectedVehicle);
        }

        void close (void)
        {
            for(unsigned int i=0; i < m_PlayerCountA ; i++)
                delete TeamA[i];
            TeamA.clear ();
            for(unsigned int i=0; i < m_PlayerCountB ; i++)
                delete TeamB[i];
            TeamB.clear ();
                    m_AllPlayers.clear();
        }

        void reset (void)
        {

            for(unsigned int i=0; i < m_PlayerCountA ; i++)
                TeamA[i]->reset ();
            for(unsigned int i=0; i < m_PlayerCountB ; i++)
                TeamB[i]->reset ();
            m_Ball->reset();
        }

        const AVGroup& allVehicles (void) {return (const AVGroup&) TeamA;}

        unsigned int	m_PlayerCountA;
        unsigned int	m_PlayerCountB;
        std::vector<Player*> TeamA;
        std::vector<Player*> TeamB;
        std::vector<Player*> m_AllPlayers;

        Ball	*m_Ball;
        AABBox	*m_bbox;
        AABBox	*m_TeamAGoal;
        AABBox	*m_TeamBGoal;
        int junk;
        int		m_redScore;
        int		m_blueScore;
    };

    MicTestPlugIn pMicTestPlugIn;

} 