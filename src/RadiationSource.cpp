#include <gazebo_radiation_plugins/RadiationSource.h>
#include <gazebo_radiation_plugins/RadiationSensor.h>

//DESTRUCTOR/CONSTRUCTOR  NEEDS TO SEARCH FOR SENSORS AND ADD/REMOVE SOURCES!!
//ADD NOISE TO SOURCE

// Do not forget to register your sensor via this block of code.
// The first argument is the Gazebo sensor type, which is how you reference the
// custom sensor in SDF. It should also match the 'name' attribute in XML plugin
// definition (together with the 'sensors/' prefix).
using gazebo::sensors::Sensor;
using gazebo::sensors::SensorFactory;


extern "C"
{
GZ_REGISTER_STATIC_SENSOR("radiation_source", RadiationSource)
}

// you can also use other sensor categories
gazebo::sensors::RadiationSource::RadiationSource()
    : Sensor(gazebo::sensors::SensorCategory::OTHER)
{
  this->active = true;
}

gazebo::sensors::RadiationSource::~RadiationSource() 
{
  
}


/////////////////////////////////////////////////
void gazebo::sensors::RadiationSource::Load(const std::string &_worldName, sdf::ElementPtr _sdf)
{
  Sensor::Load(_worldName, _sdf);
}


void gazebo::sensors::RadiationSource::Load(const std::string &_worldName)
{
  Sensor::Load(_worldName);

  this->updateConnection = event::Events::ConnectWorldUpdateBegin(
    std::bind(&gazebo::sensors::RadiationSource::UpdateImpl, this,true));


  if (this->sdf->GetElement("topic"))
  {
    this->topic = this->sdf->GetElement("topic")->Get<std::string>();
    this->name = this->topic.substr(0, this->topic.find("/"));
    this->radiation_type = this->topic.substr(this->name.size()+1, this->topic.find("/"));


    this->scanPub_pose = this->node->Advertise<msgs::Pose>(this->name+"/pose");
    this->scanPub_value = this->node->Advertise<msgs::Any>(this->name+"/value");
    this->scanPub_type = this->node->Advertise<msgs::Any>(this->name+"/type");
  
 
    this->entity = this->world->GetEntity(this->ParentName());

    n.getParam("/sources/"+this->name,params);

    this->radiation = params["value"];
    this->units = static_cast<std::string>(params["units"]);
    this->noise = params["noise"];


    // Add the tag to all the RFID sensors.
    Sensor_V sensors = SensorManager::Instance()->GetSensors();
    for (Sensor_V::iterator iter = sensors.begin(); iter != sensors.end(); ++iter)
    {

      //gzmsg << (*iter)->Type() << std::endl;
      if ((*iter)->Type() == "radiation_sensor")
      {
        std::dynamic_pointer_cast<RadiationSensor>(*iter)->AddSource(this);
        
      }
    }


  }
}


void gazebo::sensors::RadiationSource::Fini()
{

  Sensor_V sensors = SensorManager::Instance()->GetSensors();
  for (Sensor_V::iterator iter = sensors.begin(); iter != sensors.end(); ++iter)
  {

    //gzmsg << (*iter)->Type() << std::endl;
    if ((*iter)->Type() == "radiation_sensor")
    {
      std::dynamic_pointer_cast<RadiationSensor>(*iter)->RemoveSource(this->name);
      
    }
  }

  Sensor::Fini();
  this->entity.reset();
}

void gazebo::sensors::RadiationSource::Init()
{
  Sensor::Init();


}

bool gazebo::sensors::RadiationSource::UpdateImpl(const bool force)
    {

    msgs::Pose msg_pose;
    this->pose = this->GetPose();

    msgs::Set(&msg_pose, pose);

    msgs::Any msg_value;
    msg_value = msgs::ConvertAny(this->radiation + rand()/(RAND_MAX/this->noise));

    msgs::Any msg_type;
    msg_type = msgs::ConvertAny(this->radiation_type);


    this->scanPub_pose->Publish(msg_pose);
    this->scanPub_value->Publish(msg_value);   
    this->scanPub_type->Publish(msg_type);   

    }


ignition::math::Pose3d gazebo::sensors::RadiationSource::GetPose() const
{
  return this->entity->GetWorldPose().Ign();
  
}

// void gazebo::sensors::RadiationSource::SetPose() const
// {
//   //from rosparam server!!!
//   return this->entity->SetWorldPose();s
// }

 sdf::ElementPtr gazebo::sensors::RadiationSource::GetSDF(){
   return this->sdf;
 }