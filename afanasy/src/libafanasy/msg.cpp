#include "msg.h"
#include "client.h"

#include <stdio.h>
#include <string.h>

#include "../include/afanasy.h"
#include "../include/afversion.h"

#include "msgclasses/mctest.h"
#include "environment.h"
#include "address.h"

#define AFOUTPUT
#undef AFOUTPUT
#include "../include/macrooutput.h"

using namespace af;

const int Msg::Version = AFVERSION;
int Msg::Magic = AFGENERAL::MAGIC_NUMBER;

// Version + Magic + SID + Type + Integer = 20 bytes.
const int Msg::SizeHeader        = 4 + 4 + 4 + 4 + 4;

const int Msg::SizeBuffer        = 1 << 14;                 ///< Message reading buffer size = 16 Kilo bytes.
const int Msg::SizeBufferLimit   = Msg::SizeBuffer << 12;   ///< Message buffer maximum size = 67 Mega bytes.
const int Msg::SizeDataMax       = Msg::SizeBufferLimit - Msg::SizeHeader;

//
//########################## Message constructors: (and destructor) ###########################
Msg::Msg( int msgType, int msgInt, bool i_receiving)
{
    construct();
    set( msgType, msgInt, i_receiving);
}

Msg::Msg( int msgType, Af * afClass, bool i_receiving )
{
    construct();
    set( msgType, afClass, i_receiving);
}

Msg::Msg( const struct sockaddr_storage * ss):
    m_address( ss)
{
    construct();
    set( TNULL);
}

Msg::Msg( const char * rawData, int rawDataLen, bool i_receiving):
    m_receive( i_receiving),
    m_buffer( NULL),
    // This message not for write any more data. All data will be written in this constuctor.
    // We will only read node parameters to constuct af::Af based classes.
    m_writing( false)
{
    if( rawDataLen < Msg::SizeHeader ) // Check minimum message size.
    {
        AFERRAR("Msg::Msg: rawDataLen < Msg::SizeHeader (%d<%d).", rawDataLen, Msg::SizeHeader)
        setInvalid();
        return;
    }
    if( rawDataLen > Msg::SizeBufferLimit )  // Check maximum message size.
    {
        AFERRAR("Msg::Msg: rawDataLen > Msg::SizeHeader (%d>%d).", rawDataLen, Msg::SizeBufferLimit)
        setInvalid();
        return;
    }
    if( false == allocateBuffer( rawDataLen)) return; // Allocate memory
    memcpy( m_buffer, rawData, rawDataLen); // Copy raw data to the message internal buffer.
    rw_header( false);                     // Read header information from the buffer.
}

Msg::~Msg()
{
    if( m_buffer != NULL) delete [] m_buffer;
}
//
//########################## Message methods: #################################

void Msg::construct()
{
    m_version = Msg::Version;
    m_magic = Msg::Magic;
    m_type = Msg::TNULL;
    m_int32 = 0;
    m_sid = 0;

    m_receive = false;
    m_sendfailed = false;

    m_writing = false;
    m_writtensize = 0;

    m_buffer = NULL;
    allocateBuffer( Msg::SizeBuffer);
}

bool Msg::allocateBuffer( int size, int to_copy_len)
{
   if( m_type == Msg::TInvalid) return false;
   if( size > Msg::SizeBufferLimit)
   {
      AFERRAR("Msg::allocateBuffer: size > Msg::SizeBufferLimit ( %d > %d)", size, Msg::SizeBufferLimit)
      setInvalid();
      return false;
   }
   char * old_buffer = m_buffer;
   m_buffer_size = size;
AFINFA("Msg::allocateBuffer: trying %d bytes ( %d written at %p)", size, written, old_buffer)
   m_buffer = new char[m_buffer_size];
   if( m_buffer == NULL )
   {
      AFERRAR("Msg::allocateBuffer: can't allocate %d bytes for buffer.", m_buffer_size)
      setInvalid();
      return false;
   }
AFINFA("Msg::allocateBuffer: new buffer at %p", m_buffer)
   m_data         = m_buffer      + Msg::SizeHeader;
   m_data_maxsize = m_buffer_size - Msg::SizeHeader;

   if( old_buffer != NULL )
   {
      if( to_copy_len > 0) memcpy( m_data, old_buffer + Msg::SizeHeader, to_copy_len);
      delete [] old_buffer;
   }

   return true;
}

char * Msg::writtenBuffer( int size)
{
   if( m_type == Msg::TInvalid) return NULL;
//printf("Msg::writtenBuffer: size=%d, msgwrittensize=%d, address=%p\n", size, msgwrittensize, mdata + msgwrittensize);
   if( m_writtensize+size > m_data_maxsize)
   {
      int newsize = m_buffer_size << 3;
      if( m_writtensize+size > newsize) newsize = m_writtensize+size+Msg::SizeHeader;
      if( allocateBuffer( newsize, m_writtensize) == false ) return NULL;
   }
   char * wBuffer = m_data + m_writtensize;
//printf("Msg::writtenBuffer: size=%d, msgwrittensize=%d (wBuffer=%p)\n", size, msgwrittensize, wBuffer);
   m_writtensize += size;
   return wBuffer;
}

bool Msg::checkZero( bool outerror )
{
   if(( m_type  != Msg::TNULL ) ||
      ( m_int32 != 0          ) )
   {
      if( outerror )
      {
         AFERROR("Msg::checkZero(): message is non zero ( or with data).")
      }
      setInvalid();
      return false;
   }
   return true;
}

bool Msg::set( int msgType, int msgInt, bool i_receiving)
{
    m_receive = i_receiving;
    if( msgType >= Msg::TDATA)
    {
        AFERROR("Msg::set: Trying to set data message with no data.")
        setInvalid();
        return false;
    }
    m_type  = msgType;
    m_int32 = msgInt;
    rw_header( true);
    return true;
}

bool Msg::setData( int size, const char * msgData)
{
   if(checkZero( true) == false ) return false;

   m_type = Msg::TDATA;

   if(( size    <= 0                ) ||
      ( size    >  Msg::SizeDataMax ) ||
      ( msgData == NULL             ) )
   {
      AFERROR("Msg::setData(): invalid arguments.")
      setInvalid();
      return false;
   }
   m_writing = true;
   w_data( msgData, this, size);

   m_int32 = size;
   rw_header( true);
   return true;
}

bool Msg::set( int msgType, Af * afClass, bool i_receiving)
{
    if(checkZero( true) == false ) return false;

    m_receive = i_receiving;
    m_type = msgType;
    if( m_type < TDATA)
    {
        AFERROR("Msg::set: trying to set Af class with nondata type message.")
        m_type = Msg::TInvalid;
        rw_header( true);
        return false;
    }
    m_writing = true;
    afClass->write( this);
    if( m_type == Msg::TInvalid)
    {
        delete m_buffer;
        m_buffer = NULL;
        m_type = TNULL;
        allocateBuffer(100);
        w_String( "Maximum message size overload !", this);
        return false;
    }
    m_int32 = m_writtensize;
    rw_header( true);
    return true;
}

bool Msg::setString( const std::string & str)
{
    if(checkZero( true) == false ) return false;
    m_type = TString;
    m_writing = true;
    w_String( str, this);
    m_int32 = m_writtensize;
    rw_header( true);
    return true;
}

bool Msg::setStringList( const std::list<std::string> & stringlist)
{
    if(checkZero( true) == false ) return false;
    m_type = TStringList;
    m_writing = true;
    w_StringList( stringlist, this);
    m_int32 = m_writtensize;
    rw_header( true);
    return true;
}

bool Msg::getString( std::string & str)
{
   if( m_type != TString)
   {
      AFERROR("Msg::getString: type is not TString.")
      return false;
   }
   rw_String( str, this);
   // Reset written size to let to get string again.
   resetWrittenSize();
   return true;
}
const std::string Msg::getString()
{
   std::string str;
   getString( str);
   return str;
}

bool Msg::getStringList( std::list<std::string> & stringlist)
{
   if( m_type != TStringList)
   {
      AFERROR("Msg::getStringList: type is not TQStringList.")
      return false;
   }
   rw_StringList( stringlist, this);
   // Reset written size to let to get strings again.
   resetWrittenSize();
   return true;
}

bool Msg::readHeader( int bytes)
{
   rw_header( false );
   if(( m_type >= Msg::TDATA) && ( m_data_maxsize < m_int32))
   {
      allocateBuffer( m_int32+Msg::SizeHeader, bytes-Msg::SizeHeader);
      rw_header( true );
   }
   return true;
}

void Msg::readwrite( Msg * msg)
{
AFERROR("Msg::readwrite( Msg * msg): - Invalid call, use Msg::readwrite( bool write )")
}

void Msg::rw_header( bool write )
{
    if( write && ( false == checkValidness() ))
    {
        AFERROR("Msg::rw_header: Message is invalid.")
    }
    static const int int32_size = 4;
    int offset = 0;
    rw_int32( m_version, m_buffer+offset, write); offset+=int32_size;
    rw_int32( m_magic,   m_buffer+offset, write); offset+=int32_size;
    rw_int32( m_sid,     m_buffer+offset, write); offset+=int32_size;
    rw_int32( m_type,    m_buffer+offset, write); offset+=int32_size;
    rw_int32( m_int32,   m_buffer+offset, write); offset+=int32_size;
    #ifdef AFOUTPUT
    printf("Msg::readwrite: at %p: ", mbuffer); stdOut();
    #endif
    if(( false == write ) && ( false == checkValidness() ))
    {
        AFERROR("Msg::rw_header: Message is invalid.")
    }
    m_writtensize = 0;
}

bool Msg::checkValidness()
{
    if( m_type >= Msg::TLAST)
    {
        AFERRAR("Msg::checkValidness: type >= Msg::TLAST ( %d >= %d )", m_type, Msg::TLAST)
        setInvalid();
        return false;
    }
    if( m_type >= Msg::TDATA)
    {
        if( m_int32 == 0 )
        {
            AFERROR("Msg::checkValidness: data type with zero length")
            setInvalid();
            return false;
        }
        if( m_int32 > Msg::SizeDataMax )
        {
            AFERRAR("Msg::checkValidness: message with data length > Msg::SizeDataMax ( %d > %d )", m_int32, Msg::SizeDataMax)
            setInvalid();
            return false;
        }
    }
    if( m_version != Msg::Version)
    {
        AFERRAR("Msg::checkValidness: Version mismatch: Recieved(%d) != Library(%d)", m_version, Msg::Version)
        m_type = Msg::TVersionMismatch;
        m_int32 = 0;
        return true;
    }
    if( m_magic != Msg::Magic )
    {
        AFERRAR("Msg::checkValidness: Magic number mismatch: Recieved(%d) != Local(%d)", m_magic, Msg::Magic)
        m_type = Msg::TMagicMismatch;
        m_int32 = 0;
        return true;
    }
    else if( m_type == af::Msg::TMagicMismatch )
    {
        m_magic = AFGENERAL::MAGIC_NUMBER_BAD;
    }

    return true;
}

void Msg::setInvalid()
{
    m_type = Msg::TInvalid;
    m_int32 = 0;
    rw_header( true);
}

void Msg::generateInfoStream( std::ostringstream & stream, bool full) const
{
    if( m_type <= Msg::TLAST) stream << Msg::TNAMES[m_type];
    else stream << "!UNKNOWN!";
    stream << ": Length=" << writeSize() << ", type=" << m_type;
}

void Msg::stdOutData( bool withHeader)
{
   if( withHeader) stdOut( true);

   switch( m_type)
   {
   case Msg::TDATA:
   {
      if( m_data[0] != '/') printf( "%s\n", m_data);
      break;
   }
   case Msg::TTESTDATA:
   {
      MCTest( this).stdOut( true);
      break;
   }
   case Msg::TString:
   {
      std::string str;
      getString( str);
      std::cout << str;
      std::cout << std::endl;
      break;
   }
   case Msg::TStringList:
   {
      std::list<std::string> strlist;
      rw_StringList( strlist, this);
      for( std::list<std::string>::const_iterator it = strlist.begin(); it != strlist.end(); it++)
         std::cout << *it << std::endl;
      break;
   }
   }
}

const char * Msg::TNAMES[]=
{
    /*------------ NONDATA MESSAGES ----------------------*/
    /// Default message with default type - zero. Only this type can be changed by \c set function.
    "TNULL",

    /// Message set to this type itself, when reading.
    "TVersionMismatch",
    "TMagicMismatch",

    /// Invalid message. This message type generated by constructors if wrong arguments provieded.
    "TInvalid",

    "TConfirm",                   ///< Simple answer with no data to confirm something.

    /// Request messages, sizes, quantities statistics.
    "TStatRequest",

    "TConfigLoad",                ///< Reload config file
    "TFarmLoad",                  ///< Reload farm file
    "TMagicNumber",               ///< Set new Magic Number


    "TClientExitRequest",         ///< Request to client to exit,
    "TClientRestartRequest",      ///< Restart client application,
    "TClientWOLSleepRequest",     ///< Request to client to fall a sleep,
    "TClientRebootRequest",       ///< Reboot client host computer,
    "TClientShutdownRequest",     ///< Shutdown client host computer,

    /*- Talk messages -*/
    "TTalkId",                    ///< Id for new Talk. Server sends it back when new Talk registered.
    "TTalkUpdateId",              ///< Update Talk with given id ( No information for updating Talk needed).
    "TTalksListRequest",          ///< Request online Talks list.
    "TTalkDeregister",            ///< Deregister talk with given id.


    /*- Monitor messages -*/
    "TMonitorId",                 ///< Id for new Monitor. Server sends it back when new Talk registered.
    "TMonitorUpdateId",           ///< Update Monitor with given id ( No information for updating Monitor needed).
    "TMonitorsListRequest",       ///< Request online Monitors list.
    "TMonitorDeregister",         ///< Deregister monitor with given id.

    /*- Render messages -*/
    /** When Server successfully registered new Render it's send back it's id.**/
    "TRenderId",
    "TRendersListRequest",        ///< Request online Renders list message.
    "TRenderLogRequestId",        ///< Request a log of Render with given id.
    "TRenderTasksLogRequestId",   ///< Request a log of Render with given id.
    "TRenderInfoRequestId",       ///< Request a string information about a Render with given id.
    "TRenderDeregister",          ///< Deregister Render with given id.


    /*- Users messages -*/
    "TUsersListRequest",          ///< Active users information.
    /// Uset id. Afanasy sends it back as an answer on \c TUserIdRequest , which contains user name.
    "TUserId",
    "TUserLogRequestId",          ///< Request a log of User with given id.
    "TUserJobsOrderRequestId",    ///< Request User(id) jobs ids in server list order.


    /*- Job messages -*/
    "TJobsListRequest",           ///< Request brief of jobs.
    "TJobsListRequestUserId",     ///< Request brief of jobs of user with given id.
    "TJobLogRequestId",           ///< Request a log of a job with given id.
    "TJobErrorHostsRequestId",    ///< Request a list of hosts produced tasks with errors.
    "TJobsWeightRequest",         ///< Request all jobs weight.

    /// Request a job with given id. The answer is TJob. If there is no job with such id the answer is TJobRequestId.
    "TJobRequestId",
    /// Request a job progress with given id. The answer is TJobProgress. If there is no job with such id the answer is TJobProgressRequestId.
    "TJobProgressRequestId",


    "TRESERVED00",
    "TRESERVED01",
    "TRESERVED02",
    "TRESERVED03",
    "TRESERVED04",
    "TRESERVED05",
    "TRESERVED06",
    "TRESERVED07",
    "TRESERVED08",
    "TRESERVED09",

    /*---------------------------------------------------------------------------------------------------------*/
    /*--------------------------------- DATA MESSAGES ---------------------------------------------------------*/
    /*---------------------------------------------------------------------------------------------------------*/


    "TDATA",                      ///< Some data.
    "TTESTDATA",                  ///< Test some data transfer.
    "TString",                    ///< QString text message.
    "TStringList",                ///< QStringList text message.

    "TStatData",                  ///< Statistics data.

    /*- Client messages -*/

    /*- Talk messages -*/
    /// Register Talk. Send by Talk client to register. Server sends back its id \c TTalkId.
    "TTalkRegister",
    "TTalksListRequestIds",       ///< Request a list of Talks with given ids.
    "TTalksList",                 ///< Message with a list of online Talks.
    "TTalkDistributeData",        ///< Message with a list Talk's users and a text to send to them.
    "TTalkData",                  ///< Message to Talk with text.
    "TTalkExit",                  ///< Ask server to shutdown client application(s),


    /*- Monitor messages -*/
    /// Register Monitor. Send by Monitor client to register. Server sends back its id \c TMonitorId.
    "TMonitorRegister",
    "TMonitorsListRequestIds",    ///< Request a list of Monitors with given ids.
    "TMonitorsList",              ///< Message with a list of online Monitors.
    "TMonitorSubscribe",          ///< Subscribe monitor on some events.
    "TMonitorUnsubscribe",        ///< Unsubscribe monitor from some events.
    "TMonitorUsersJobs",          ///< Set users ids to monitor their jobs.
    "TMonitorJobsIdsAdd",         ///< Add jobs ids for monitoring.
    "TMonitorJobsIdsSet",         ///< Set jobs ids for monitoring.
    "TMonitorJobsIdsDel",         ///< Delete monitoring jobs ids.
    "TMonitorMessage",            ///< Send a message (TQString) to monitors with provieded ids (MCGeneral).
    "TMonitorExit",               ///< Ask server to shutdown client application(s),

    "TMonitorEvents_BEGIN",       ///< Events types start.

    "TMonitorJobEvents_BEGIN",    ///< Job events types start.
    "TMonitorJobsAdd",            ///< IDs of new jobs.
    "TMonitorJobsChanged",        ///< IDs of changed jobs.
    "TMonitorJobsDel",            ///< IDs of deleted jobs.
    "TMonitorJobEvents_END",      ///< Job events types end.

    "TMonitorCommonEvents_BEGIN", ///< Common events types start.
    "TMonitorUsersAdd",           ///< IDs of new users.
    "TMonitorUsersChanged",       ///< IDs of changed users.
    "TMonitorUsersDel",           ///< IDs of deleted users.
    "TMonitorRendersAdd",         ///< IDs of new renders.
    "TMonitorRendersChanged",     ///< IDs of changed renders.
    "TMonitorRendersDel",         ///< IDs of deleted renders.
    "TMonitorMonitorsAdd",        ///< IDs of new monitors.
    "TMonitorMonitorsChanged",    ///< IDs of changed monitors.
    "TMonitorMonitorsDel",        ///< IDs of deleted monitors.
    "TMonitorTalksAdd",           ///< IDs of new talks.
    "TMonitorTalksDel",           ///< IDs of deleted talks.
    "TMonitorCommonEvents_END",   ///< Common events types end.

    "TMonitorEvents_END",         ///< Events types end.


    /*- Render messages -*/
    /** Sent by Render on start, when it's server begin to listen post.
    And when Render can't connect to Afanasy. Afanasy register new Render and send back it's id \c TRenderId. **/
    "TRenderRegister",
    "TRenderUpdate",              ///< Update Render, message contains its resources.
    "TRendersListRequestIds",     ///< Request a list of Renders with given ids.
    "TRendersUpdateRequestIds",   ///< Request a list of resources of Renders with given ids.
    "TRendersList",               ///< Message with a list of Renders.
    "TRendersListUpdates",        ///< Message with a list of resources of Renders.
    "TRenderSetPriority",         ///< Set Render priority,
    "TRenderSetCapacity",         ///< Set Render capacity,
    "TRenderSetMaxTasks",         ///< Set Render maximum tasks,
    "TRenderSetService",          ///< Enable or disable Render service,
    "TRenderRestoreDefaults",     ///< Restore default Render settings,
    "TRenderSetNIMBY",            ///< Set Render NIMBY,
    "TRenderSetUser",             ///< Set Render user,
    "TRenderSetNimby",            ///< Set Render nimby,
    "TRenderSetFree",             ///< Set Render free,
    "TRenderStopTask",            ///< Signal from Afanasy to Render to stop task.
    "TRenderCloseTask",           ///< Signal from Afanasy to Render to close (delete) finished (stopped) task.
    "TRenderEjectTasks",          ///< Stop all tasks on Render,
    "TRenderDelete",              ///< Delete Render from afanasy server container and database,
    "TRenderRestart",             ///< Restart Render client program,
    "TRenderWOLSleep",            ///< Ask online render(s) to fall into sleep
    "TRenderWOLWake",             ///< Ask sleeping render(s) to wake up
    "TRenderReboot",              ///< Reboot Render host computer,
    "TRenderShutdown",            ///< Shutdown Render host computer,
    "TRenderAnnotate",            ///< Set Render annotation,
    "TRenderExit",                ///< Ask server to shutdown client application(s),
    "TRenderEjectNotMyTasks",     ///< Stop mot my (based on username of message) tasks on Render,


    /*- Users messages -*/
    "TUsersListRequestIds",       ///< Request a list of Users with given ids.
    "TUsersList",                 ///< Active users information.
    "TUserAdd",                   ///< Add a permatent user.
    "TUserDel",                   ///< Remove a permatent user.
    "TUserJobsLifeTime",          ///< Set user jobs default life time.
    "TUserHostsMask",             ///< Set user hosts mask.
    "TUserHostsMaskExclude",      ///< Set user exclude hosts mask.
    "TUserMaxRunningTasks",       ///< Set user maximum running tasks number.
    "TUserPriority",              ///< Set user priority.
    "TUserErrorsAvoidHost",       ///< Set number of errors on host to avoid it.
    "TUserErrorRetries",          ///< Set number of automatic retries task with errors.
    "TUserErrorsTaskSameHost",    ///< Set number of errors for task on same host.
    "TUserErrorsForgiveTime",     ///< Set time to forgive error host.
    "TUserIdRequest",             ///< Request an id of user with given name.
    "TUserMoveJobsUp",            ///< Move jobs one position up in user jobs list.
    "TUserMoveJobsDown",          ///< Move jobs one position down in user jobs list.
    "TUserMoveJobsTop",           ///< Move jobs to top position in user jobs list.
    "TUserMoveJobsBottom",        ///< Move jobs to bottom position up in user jobs list.
    "TUserJobsOrder",             ///< Jobs ids in server list order.
    "TUserAnnotate",              ///< Set User annotation,
    "TUserJobsSolveMethod",       ///< Set jobs solving mode.


    /*- Job messages -*/
    "TJobRegister",               ///< Register job.
    "TJobStart",                  ///< Start offline (paused) job.
    "TJobStop",                   ///< Stop job ( stop running tasks and set offline state).
    "TJobRestart",                ///< Restart job.
    "TJobRestartErrors",          ///< Restart tasks with errors.
    "TJobResetErrorHosts",        ///< Reset all job blocks error hosts.
    "TJobPause",                  ///< Pause job ( set offline state, keep running tasks running).
    "TJobRestartPause",           ///< Restart and pause job.
    "TJobDelete",                 ///< Delete a job.
    "TJobsListRequestIds",        ///< Request a list of Jobs with given ids.
    "TJobsListRequestUsersIds",   ///< Request brief of jobs od users with given ids.
    "TJobsList",                  ///< Jobs list information.
    "TJobProgress",               ///< Jobs progress.
    "TJobHostsMask",              ///< Set job hosts mask.
    "TJobHostsMaskExclude",       ///< Set job exclude hosts mask.
    "TJobDependMask",             ///< Set job depend mask.
    "TJobDependMaskGlobal",       ///< Set job global depend mask.
    "TJobMaxRunningTasks",        ///< Set job maximum running tasks number.
    "TJobMaxRunTasksPerHost",     ///< Set job maximum running tasks per host.
    "TJobWaitTime",               ///< Set job wait time.
    "TJobLifeTime",               ///< Set job life time.
    "TJobPriority",               ///< Set job priority.
    "TJobNeedOS",                 ///< Set a job os needed.
    "TJobNeedProperties",         ///< Set a job properties needed.
    "TJobsWeight",                ///< All jobs weight data.
    "TJobCmdPost",                ///< Set job post command.
    "TJobAnnotate",               ///< Set Job annotation,
    "TJobSetUser",                ///< Change job owner.
    "TJob",                       ///< Job (all job data).

    "TBlockDependMask",           ///< Set block depend mask.
    "TBlockTasksDependMask",      ///< Set block tasks depend mask.
    "TBlockTasksMaxRunTime",      ///< Set block tasks maximum run time.
    "TBlockHostsMask",            ///< Set block hosts mask.
    "TBlockHostsMaskExclude",     ///< Set block exclude hosts mask.
    "TBlockMaxRunningTasks",      ///< Set block maximum running tasks number.
    "TBlockMaxRunTasksPerHost",   ///< Set block maximum running tasks number on the same host.
    "TBlockCommand",              ///< Set block command.
    "TBlockWorkingDir",           ///< Set block working directory.
    "TBlockFiles",                ///< Set block files.
    "TBlockCmdPost",              ///< Set block post command.
    "TBlockService",              ///< Set block task type.
    "TBlockParser",               ///< Set block parser type.
    "TBlockParserCoeff",          ///< Set block parser coefficient.
    "TBlockResetErrorHosts",      ///< Reset block avoid hosts.
    "TBlockErrorsAvoidHost",      ///< Set number of errors on host to avoid it.
    "TBlockErrorRetries",         ///< Set number of automatic retries task with errors.
    "TBlockErrorsSameHost",       ///< Set number of errors for the task on same host to make task to avoid host
    "TBlockErrorsForgiveTime",    ///< Set time to forgive error host.
    "TBlockCapacity",             ///< Set block capacity.
    "TBlockCapacityCoeffMin",     ///< Set block capacity minimum coefficient.
    "TBlockCapacityCoeffMax",     ///< Set block capacity maximum coefficient.
    "TBlockMultiHostMin",         ///< Set block multihost minimum hosts count.
    "TBlockMultiHostMax",         ///< Set block multihost maximum hosts count.
    "TBlockMultiHostWaitMax",     ///< Set block multihost maximum hosts wait time.
    "TBlockMultiHostWaitSrv",     ///< Set block multihost service start wait time.
    "TBlockNeedMemory",           ///< Set block render memory need.
    "TBlockNeedPower",            ///< Set block render power need.
    "TBlockNeedHDD",              ///< Set block render hdd need.
    "TBlockNeedProperties",       ///< Set block render properties need.
    "TBlocksProgress",            ///< Blocks running progress data.
    "TBlocksProperties",          ///< Blocks progress and properties data.
    "TBlocks",                    ///< Blocks data.

    "TTask",                      ///< A task of some job.
    "TTasksSkip",                 ///< Skip some tasks.
    "TTasksRestart",              ///< Restart some tasks.
    "TTaskRequest",               ///< Get task information.
    "TTaskLogRequest",            ///< Get task information log.
    "TTaskErrorHostsRequest",     ///< Get task error hosts list.
    "TTaskOutputRequest",         ///< Job task output request.
    "TTaskUpdatePercent",         ///< New progress percentage for task.
    "TTaskUpdateState",           ///< New state for task.
    "TTaskListenOutput",          ///< Request to send task output to provided address.
    "TTaskOutput",                ///< Job task output data.
    "TTasksRun",                  ///< Job tasks run data.

    "TRESERVED10",
    "TRESERVED11",
    "TRESERVED12",
    "TRESERVED13",
    "TRESERVED14",
    "TRESERVED15",
    "TRESERVED16",
	"TBlockNonSequential",        ///< Set block task solving to non-sequential.
	"TRenderHideShow",            ///< Hide or show renders.
	"TJobHideShow",               ///< Hide or show jobs.

    "TLAST"                       ///< The last type number.
};