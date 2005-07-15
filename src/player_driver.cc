/*
 *  Stage plugin driver for Player
 *  Copyright (C) 2004-2005 Richard Vaughan
 *                      
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * Desc: A plugin driver for Player that gives access to Stage devices.
 * Author: Richard Vaughan
 * Date: 10 December 2004
 * CVS: $Id: player_driver.cc,v 1.21 2005-07-14 23:37:23 rtv Exp $
 */

// DOCUMENTATION ------------------------------------------------------------

/** @defgroup driver_stage Stage plugin driver for Player

This driver gives Player access to Stage's models.

@par Provides

The stage plugin driver provides the following device interfaces and
configuration requests. The name of each interface
is a link to its entry in the Player manual:

- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__blobfinder.html">blobfinder</a>
 - (none)

- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__fiducial.html">fiducial</a>
  - PLAYER_FIDUCIAL_GET_GEOM_REQ
  - PLAYER_FIDUCIAL_SET_FOV_REQ
  - PLAYER_FIDUCIAL_GET_FOV_REQ
  - PLAYER_FIDUCIAL_SET_ID_REQ
  - PLAYER_FIDUCIAL_GET_ID_REQ

- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__laser.html">laser</a>
 - PLAYER_LASER_SET_CONFIG
 - PLAYER_LASER_SET_CONFIG
 - PLAYER_LASER_GET_GEOM

- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__position.html">position</a>
  - PLAYER_POSITION_SET_ODOM_REQ
  - PLAYER_POSITION_RESET_ODOM_REQ
  - PLAYER_POSITION_GET_GEOM_REQ
  - PLAYER_POSITION_MOTOR_POWER_REQ
  - PLAYER_POSITION_VELOCITY_MODE_REQ

- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__sonar.html">sonar</a>
  - PLAYER_SONAR_GET_GEOM_REQ

s- <a href="http://playerstage.sourceforge.net/doc/Player-1.6-html/player/group__player__interface__simulation.html">simulation</a>
  - (none)



@par Player configuration file options

- model (string)
  - where (string) is the name of a Stage position model that will be controlled by this interface. Stage will search the tree of models below the named model to find a device of the right type.
  
@par Configuration file examples:

Creating models in a Stage worldfile, saved as "example.world":

@verbatim
# create a position model - it can drive around like a robot
position
(
  pose [ 1 1 0 ]
  color "red"
  name "marvin"

  # add a laser scanner on top of the robot
  laser() 
)
@endverbatim


Using Stage models in a Player config (.cfg) file:

@verbatim
# load the Stage plugin and create a world from a worldfile
driver
(		
  name "stage"
  provides ["simulation:0"]
  plugin "libstageplugin"

  # create the simulated world described by this worldfile
  worldfile "example.world"	
)

# create a position device, connected to a Stage position model 
driver
(
  name "stage"
  provides ["position:0" ]
  model "marvin"
)

# create a laser device, connected to a Stage laser model
driver
(
  name "stage"
  provides ["laser:0" ]
  model "marvin"
)
@endverbatim

More examples can be found in the Stage source tree, in directory
<stage-version>/worlds.

@par Authors

Richard Vaughan
*/


// TODO - configs I should implement
//  - PLAYER_SONAR_POWER_REQ
//  - PLAYER_BLOBFINDER_SET_COLOR_REQ
//  - PLAYER_BLOBFINDER_SET_IMAGER_PARAMS_REQ

// CODE ------------------------------------------------------------

#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <math.h>
#include <player/drivertable.h>

#include "player_driver.h"
#include "player_interfaces.h"
#include "zoo_driver.h"

#define STG_DEFAULT_WORLDFILE "default.world"
#define DRIVER_ERROR(X) printf( "Stage driver error: %s\n", X )

// globals from Player
extern PlayerTime* GlobalTime;
extern int global_argc;
extern char** global_argv;
extern bool quiet_startup;

// init static vars
stg_world_t* StgDriver::world = NULL;


/* need the extern to avoid C++ name-mangling  */
extern "C"
{
  int player_driver_init(DriverTable* table);
}

// A factory creation function, declared outside of the class so that it
// can be invoked without any object context (alternatively, you can
// declare it static in the class).  In this function, we create and return
// (as a generic Driver*) a pointer to a new instance of this driver.
Driver* StgDriver_Init(ConfigFile* cf, int section)
{
  // Create and return a new instance of this driver
  return ((Driver*) (new StgDriver(cf, section)));
}
  
// A driver registration function, again declared outside of the class so
// that it can be invoked without object context.  In this function, we add
// the driver into the given driver table, indicating which interface the
// driver can support and how to create a driver instance.
void StgDriver_Register(DriverTable* table)
{
  printf( "\n ** Stage plugin v%s **", PACKAGE_VERSION );
    
  if( !quiet_startup )
    {
      puts( "\n * Part of the Player/Stage Project [http://playerstage.sourceforge.net]\n"
	    " * Copyright 2000-2005 Richard Vaughan, Andrew Howard, Brian Gerkey\n * and contributors.\n"
	    " * Released under the GNU GPL." );
    }
    
  table->AddDriver( "stage", StgDriver_Init);
}
  
int player_driver_init(DriverTable* table)
{
  //puts(" Stage driver plugin init");
  StgDriver_Register(table);
  ZooDriver_Register(table);
  return(0);
}

// find a model to attach to a Player interface
stg_model_t* model_match( stg_model_t* mod, stg_model_type_t tp, GPtrArray* devices )
{
  if( mod->type == tp )
    return mod;
 
  // else try the children
  stg_model_t* match=NULL;

  // for each model in the child list
  for(int i=0; i<(int)mod->children->len; i++ )
    {
      // recurse
      match = 
	model_match( (stg_model_t*)g_ptr_array_index( mod->children, i ), 
		     tp, devices );      
      if( match )
	{
	  // if mod appears in devices already, it can not be used now
	  //printf( "[inspecting %d devices used already]", devices->len );
	  for( int i=0; i<(int)devices->len; i++ )
	    {
	      Interface* interface = 
		(Interface*)g_ptr_array_index( devices, i );
	      
	      //printf( "comparing %p and %p (%d.%d.%d)\n", mod, record->mod,
	      //      record->id.port, record->id.code, record->id.index );
	      
	      if( match == interface->mod )//&& ! tp == STG_MODEL_BASIC )
		{
		  printf( "[ALREADY USED]" );
		  return NULL;
		}
	    }
	  // if we found a match, we're done searching
	  return match;
	}
    }

  return NULL;
}


// Constructor.  Retrieve options from the configuration file and do any
// pre-Setup() setup.
StgDriver::StgDriver(ConfigFile* cf, int section)
    : Driver(cf, section)
{  
  // init the array of device ids
  this->devices = g_ptr_array_new();
  
  int device_count = cf->GetTupleCount( section, "provides" );
  
  if( !quiet_startup )
    {  
      printf( "  Stage driver creating %d %s\n", 
	      device_count, 
	      device_count == 1 ? "device" : "devices" );
    }
  
  for( int d=0; d<device_count; d++ )
    {
      player_device_id_t player_id;
      
      if (cf->ReadDeviceId( &player_id, section, "provides", 0, d, NULL) != 0)
	{
	  this->SetError(-1);
	  return;
	}  
            
      if( !quiet_startup )
	{
	  printf( "    mapping %d.%d.%d => ", 
		  player_id.port, player_id.code, player_id.index );
	  fflush(stdout);
	}
      
      
      Interface *ifsrc = NULL;

      switch( player_id.code )
	{
	case PLAYER_SIMULATION_CODE:
	  ifsrc = new InterfaceSimulation( player_id, this, cf, section );
	  break;
	  
	case PLAYER_POSITION_CODE:	  
	  ifsrc = new InterfacePosition( player_id, this,  cf, section );
	  break;
	  
	case PLAYER_LASER_CODE:	  
	  ifsrc = new InterfaceLaser( player_id,  this, cf, section );
	  break;

	case PLAYER_FIDUCIAL_CODE:
	  ifsrc = new InterfaceFiducial( player_id,  this, cf, section );
	  break;
	  
	case PLAYER_BLOBFINDER_CODE:
	  ifsrc = new InterfaceBlobfinder( player_id,  this, cf, section );
	  break;
	  
	case PLAYER_SONAR_CODE:
	  ifsrc = new InterfaceSonar( player_id,  this, cf, section );
	  break;

	  /*	  
	case PLAYER_GRIPPER_CODE:
	  break;
	  	  	  
	case PLAYER_MAP_CODE:
	  break;
	  */

	default:
	  PRINT_ERR1( "error: stage driver doesn't support interface type %d\n",
		      player_id.code );
	  this->SetError(-1);
	  return; 
	}
      
      if( ifsrc )
	{
	  // attempt to add this interface and we're done
	  if( this->AddInterface( ifsrc->id, 
				  PLAYER_ALL_MODE, 
				  ifsrc->data_len, 
				  ifsrc->cmd_len,
				  ifsrc->req_qlen,
				  ifsrc->req_qlen ) )
	    {
	      DRIVER_ERROR( "AddInterface() failed" );
	      this->SetError(-2);
	      return;
	    }
	  
	  // store the Interaface in our device list
	  g_ptr_array_add( this->devices, ifsrc );
	}
      else
	{
	  PRINT_ERR3( "No Stage source found for interface %d:%d:%d",
		      player_id.port, player_id.code, player_id.index );
	  
	  this->SetError(-3);
	  return;
	} 
    }
  //puts( "  Stage driver loaded successfully." );
}

stg_model_t*  StgDriver::LocateModel( const char* basename,  
				      stg_model_type_t mod_type )
{  
  //PLAYER_TRACE1( "attempting to resolve Stage model \"%s\"", model_name );
  //printf( "attempting to resolve Stage model \"%s\"", model_name );
  
  stg_model_t* base_model = 
    stg_world_model_name_lookup( StgDriver::world, basename );
  
  if( base_model == NULL )
    {
      PRINT_ERR1( " Error! can't find a Stage model named \"%s\"", 
		  basename );
      return NULL;
    }
  
  // printf( "found base model %s\n", base_model->token );
  
  // todo
  // map interface can attach only to the base model
  //if( device->id.code == PLAYER_MAP_CODE )
  //return base_model;
  
  // now find the model for this player device
  // find the first model in the tree that is the right type and
  // has not been used before
  return( model_match( base_model, mod_type, this->devices ) );
}

////////////////////////////////////////////////////////////////////////////////
// Set up the device.  Return 0 if things go well, and -1 otherwise.
int StgDriver::Setup()
{   
  //puts("stage driver setup");  
  return(0);
}

// find the device record with this Player id
// todo - faster lookup with a better data structure
Interface* StgDriver::LookupDevice( player_device_id_t id )
{
  for( int i=0; i<(int)this->devices->len; i++ )
    {
      Interface* candidate = 
	(Interface*)g_ptr_array_index( this->devices, i );
      
      if( candidate->id.port == id.port &&
	  candidate->id.code == id.code &&
	  candidate->id.index == id.index )
	return candidate; // found
    }

  return NULL; // not found
}


// subscribe to a device
int StgDriver::Subscribe(player_device_id_t id)
{
  if( id.code == PLAYER_SIMULATION_CODE )
    return 0; // ok

  Interface* device = this->LookupDevice( id );
  
  if( device )
    {      
      stg_model_subscribe( device->mod );  
      return Driver::Subscribe(id);
    }

  puts( "failed to find a device" );
  return 1; // error
}


// unsubscribe to a device
int StgDriver::Unsubscribe(player_device_id_t id)
{
  if( id.code == PLAYER_SIMULATION_CODE )
    return 0; // ok

  Interface* device = this->LookupDevice( id );
  
  if( device )
    {
      stg_model_unsubscribe( device->mod );  
      return Driver::Unsubscribe(id);
    }
  else
    return 1; // error
}

StgDriver::~StgDriver()
{
  // todo - when the sim thread exits, destroy the world. It's not
  // urgent, because right now this only happens when Player quits.
  // stg_world_destroy( StgDriver::world );

  //puts( "Stage driver destroyed" );
}


////////////////////////////////////////////////////////////////////////////////
// Shutdown the device
int StgDriver::Shutdown()
{
  puts("Shutting stage driver down");

  // Stop and join the driver thread
  // this->StopThread(); // todo - the thread only runs in the sim instance

  // shutting unsubscribe to data from all the devices
  //for( int i=0; i<(int)this->devices->len; i++ )
  //{
  //  Interface* device = (Interface*)g_ptr_array_index( this->devices, i );
  //  stg_model_unsubscribe( device->mod );
  // }

  puts("stage driver has been shutdown");

  return(0);
}


void StgDriver::Main( void )
{
  assert( this->world );

  // The main loop; interact with the device here
  for(;;)
  {
    // test if we are supposed to cancel
    //pthread_testcancel();

    if( stg_world_update( StgDriver::world, TRUE ) )
      exit( 0 );

    // todo - sleep out here only if we can afford the time
    //usleep( 10000 ); // 1ms
  }
}

// Moved this declaration here (and changed its name) because 
// when it's declared on the stack inside StgDriver::Update below, it 
// corrupts the stack.  This makes makes valgrind unhappy (errors about
// invalid reads/writes to another thread's stack.  This may also be related
// to occasional mysterious crashed with Stage that I've seen.
//        - BPG
uint8_t stage_buffer[ PLAYER_MAX_MESSAGE_SIZE ];

void StgDriver::Update(void)
{  
  for( int i=0; i<(int)this->devices->len; i++ )
    {
      Interface* interface = (Interface*)g_ptr_array_index( this->devices, i );
      
      //printf( "checking command for %d:%d:%d\n",
      //      device->id.port, 
      //      device->id.code, 
      //      device->id.index );
      
      if( interface && interface->cmd_len > 0 )
	{    
	  // copy the command from Player
	  size_t cmd_len = 
	    this->GetCommand( interface->id, stage_buffer, 
			      interface->cmd_len, NULL);
	  
	  //printf( "command for %d:%d:%d is %d bytes\n",
	  //  interface->id.port, 
	  //  interface->id.code, 
	  //  interface->id.index, 
	  //  (int)cmd_len );
	  
	  if( cmd_len > 0 )
	    interface->Command( stage_buffer, cmd_len );
	}
      
      // check for configs
      if( interface && interface->req_qlen > 0 )
	{
	  void* client = NULL;
	  int cfg_len = 
	    this->GetConfig( interface->id, &client, 
			     stage_buffer, PLAYER_MAX_MESSAGE_SIZE, NULL );
	  
	  if( cfg_len > 0 )
	    interface->Configure( client, stage_buffer, cfg_len );
	}
      
      interface->Publish();
    }
  
  // update the world
  return;
}