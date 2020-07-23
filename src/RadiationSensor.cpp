#include <gazebo_radiation_plugins/RadiationSensor.h>
#include <gazebo_radiation_plugins/RadiationSource.h>

// ADD A WAY OFF DECIDING OF ITS COLLIMATED OR NOT/Type of sensor

//maybe include ros and do rosparam

// Do not forget to register your sensor via this block of code.
// The first argument is the Gazebo sensor type, which is how you reference the
// custom sensor in SDF. It should also match the 'name' attribute in XML plugin
// definition (together with the 'sensors/' prefix).
using gazebo::sensors::Sensor;
using gazebo::sensors::SensorFactory;

/*
The only way to add custom elements to the sdf is to dig into the the sdf definintions
Pull stuff from ros param server !!! like radiation
Write a script to get the radiation source file from the output yaml!!!
*/

extern "C"
{
  GZ_REGISTER_STATIC_SENSOR("radiation_sensor", RadiationSensor)
}

// you can also use other sensor categories
gazebo::sensors::RadiationSensor::RadiationSensor()
    : Sensor(gazebo::sensors::SensorCategory::OTHER)
{
  this->active = true;
}

gazebo::sensors::RadiationSensor::~RadiationSensor()
{
}

/////////////////////////////////////////////////
void gazebo::sensors::RadiationSensor::Load(const std::string &_worldName, sdf::ElementPtr _sdf)
{
  Sensor::Load(_worldName, _sdf);
}

void gazebo::sensors::RadiationSensor::Load(const std::string &_worldName)
{
  Sensor::Load(_worldName);

  this->updateConnection = event::Events::ConnectWorldUpdateBegin(
      std::bind(&gazebo::sensors::RadiationSensor::UpdateImpl, this, true));

  if (this->sdf->GetElement("topic"))
  {
    this->scanPub_pose = this->node->Advertise<msgs::Pose>(this->sdf->GetElement("topic")->Get<std::string>() + "/pose");
    this->scanPub_value = this->node->Advertise<msgs::Any>(this->sdf->GetElement("topic")->Get<std::string>() + "/value");
  }

  this->topic = this->sdf->GetElement("topic")->Get<std::string>();

  //this->sensor_type = this->sdf->GetElement("sensor_type")->Get<std::string>();
  this->sensor_type = "";

  this->entity = this->world->GetEntity(this->ParentName());

  if (n.hasParam("sensors/" + this->topic + "/type"))
  {
    n.getParam("sensors/" + this->topic + "/type", this->sensor_type);
  }

  if (n.hasParam("sensors/" + this->topic + "/range"))
  {
    n.getParam("sensors/" + this->topic + "/range", this->sensor_range);
  }

  if (n.hasParam("sensors/" + this->topic + "/collimated"))
  {
    n.getParam("sensors/" + this->topic + "/collimated", this->collimated);
  } 

  if (n.hasParam("sensors/" + this->topic + "/sensitivity_function"))
  {
    n.getParam("sensors/" + this->topic + "/sensitivity_function", this->sensitivity_func);
  }  

  if (n.hasParam("sensors/" + this->topic + "/mu"))
  {
    n.getParam("sensors/" + this->topic + "/mu", this->mu);
  }

  if (n.hasParam("sensors/" + this->topic + "/sigma"))
  {
    n.getParam("sensors/" + this->topic + "/sigma", this->sig);
  }

  if (n.hasParam("sensors/" + this->topic + "/angle_limit"))
  {
    n.getParam("sensors/" + this->topic + "/angle_limit", this->angle_limit);
  }

  if (n.hasParam("/attenuation_factors"))
  {
    n.getParam("/attenuation_factors", this->attenuation_factors);
  }
  


  Sensor_V sensors = SensorManager::Instance()->GetSensors();
  for (Sensor_V::iterator iter = sensors.begin(); iter != sensors.end(); ++iter)
  {


    if ((*iter)->Type() == "radiation_source")
    {

      auto sensorPtr = std::static_pointer_cast<RadiationSource>(*iter).get();
      this->AddSource(sensorPtr);
    }
  }

  this->blockingRay = boost::dynamic_pointer_cast<gazebo::physics::RayShape>(
      this->world->GetPhysicsEngine()->CreateShape("ray", gazebo::physics::CollisionPtr()));
}

void gazebo::sensors::RadiationSensor::Fini()
{

  Sensor::Fini();
  this->entity.reset();
}

void gazebo::sensors::RadiationSensor::Init()
{
  Sensor::Init();
}

bool gazebo::sensors::RadiationSensor::UpdateImpl(const bool force)
{

  msgs::Pose msg_pose;
  this->pose = this->GetPose();
  msgs::Set(&msg_pose, pose);

  this->EvaluateSources();
  msgs::Any msg_value;
  msg_value = msgs::ConvertAny(this->radiation);

  this->scanPub_pose->Publish(msg_pose);
  this->scanPub_value->Publish(msg_value);

  return true;
}

//////////////////////////////////////////////////
void gazebo::sensors::RadiationSensor::EvaluateSources()
{
  std::vector<RadiationSource *>::const_iterator ci;

  // iterate through the tags contained given rfid tag manager

  double rad = 0.0;
  for (ci = this->sources.begin(); ci != this->sources.end(); ci++)
  {
    ignition::math::Pose3d pos = (*ci)->GetPose();
    double dist = this->CheckSourceRange(pos);
    if (dist <= this->sensor_range)
    {
      std::vector<raySegment> raySegments = CheckSourceViewable(this->entity->GetWorldPose().Ign().Pos(), pos.Pos(), (*ci)->name);

      std::string type = (*ci)->radiation_type;
      double value = (*ci)->radiation;

      double sensitivity;
      double angle = this->CheckSourceAngle(pos);
      if (this->collimated == true)
      {
        sensitivity = sensitivity_function(angle);
      }
      else
      {
        sensitivity = 1.0;
      }
      float within_angle_limit = 1.0;  
      if (fabs(this->angle_limit) <= fabs(angle) ){
        within_angle_limit = 0.0;
      }
      float within_range_limit = 1.0;  
      if (fabs(this->sensor_range) <= fabs(dist) ){
        within_range_limit = 0.0;
      }

      if (raySegments.empty())
      {
        rad += within_range_limit*within_angle_limit * sensitivity * value / ((dist * dist) + (+3.3E-5 * 3.3E-5));
      }
      else
      {
        rad += within_range_limit*within_angle_limit * sensitivity * value / ((dist * dist) + (+3.3E-5 * 3.3E-5)) * AttenuationFactor(raySegments);
      }
    }
  }

  if ((rand() / RAND_MAX) < (rad - floor(rad)))
  {
    this->radiation = ceil(rad);
  }
  else
  {
    this->radiation = floor(rad);
  }
}

double gazebo::sensors::RadiationSensor::AttenuationFactor(std::vector<raySegment> ray_vector)
{

  //gzmsg << "ray interations :" << std::endl;
  double attenuation_factor = 1.0;
  double material_attenuation = 0.0;
  XmlRpc::XmlRpcValue y;

  for (int i = 0; i < ray_vector.size(); i++)
  {
    //gzmsg << "transition from " << ray_vector[i].from << " to " << ray_vector[i].to << " at length: " << ray_vector[i].length << std::endl;

    for (XmlRpc::XmlRpcValue::ValueStruct::const_iterator it = this->attenuation_factors.begin();
         it != this->attenuation_factors.end(); ++it)
    {
      XmlRpc::XmlRpcValue x = it->first;
      if (ray_vector[i].from.find(std::string(x)) != std::string::npos)
      {
        y = it->second;
        if (static_cast<double>(y) < 0.0)
        {
          material_attenuation = 0;
          attenuation_factor = 0.0;
        }
        else
        {
          material_attenuation = static_cast<double>(y);
       } //break;
      }
    }

    //old fake attenuation factor
    //attenuation_factor *= 1.0 - (ray_vector[i].length*material_attenuation);
    //andys attenuation factor
    attenuation_factor *= exp(-material_attenuation * ray_vector[i].length);

  }

  /*
  Add function here to do attenuation along the line!!!
  */
  return attenuation_factor;
}

double gazebo::sensors::RadiationSensor::sensitivity_function(double x)
{
  if (this->sensitivity_func == "gaussian"){
    return exp(-pow(x - this->mu, 2.0) / (2.0 * pow(this->sig, 2.0)));
  } 
  else{
    return 1.0;
  }
}

//////////////////////////////////////////////////
double gazebo::sensors::RadiationSensor::CheckSourceRange(const ignition::math::Pose3d &_pose)
{
  // copy sensor vector pos into a temp var
  ignition::math::Vector3d v;
  v = _pose.Pos() - this->entity->GetWorldPose().Ign().Pos();

  return v.Length();
}
//////////////////////////////////////////////////
float dot(ignition::math::Vector3d a, ignition::math::Vector3d b) //calculates dot product of a and b
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float mag(ignition::math::Vector3d a) //calculates magnitude of a
{
  return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

double gazebo::sensors::RadiationSensor::CheckSourceAngle(const ignition::math::Pose3d &_pose)
{
  // copy sensor vector pos into a temp var
  ignition::math::Vector3d v0;
  ignition::math::Vector3d v1;
  ignition::math::Vector3d v(1, 0, 0);

  v0 = _pose.Pos() - entity->GetWorldPose().Ign().Pos();
  v1 = entity->GetWorldPose().Ign().Rot().RotateVector(v);

  double angle = std::acos(dot(v0, v1) / (mag(v0) * mag(v1)));

  return angle;
}

//////////////////////////////////////////////////
ignition::math::Pose3d gazebo::sensors::RadiationSensor::GetPose() const
{
  return this->entity->GetWorldPose().Ign();
}

void gazebo::sensors::RadiationSensor::AddSource(RadiationSource *_rs)
{
  if (this->sensor_type == "")
  {
    gzmsg << "adding source " << _rs->GetSDF()->GetElement("topic")->Get<std::string>() << std::endl;

    this->sources.push_back(_rs);
  }
  else if (_rs->radiation_type == this->sensor_type)
  {
    gzmsg << "adding source " << _rs->GetSDF()->GetElement("topic")->Get<std::string>() << std::endl;

    this->sources.push_back(_rs);
  }
}

void gazebo::sensors::RadiationSensor::RemoveSource(std::string source_name)
{
  for (int i = 0; i < this->sources.size(); i++)
  {
    if (this->sources[i]->name == source_name)
    {
      gzmsg << this->sources[i]->name << " removed " << this->sources.size() << std::endl;
      this->sources.erase(this->sources.begin() + i);
      i--;
    }
  }
}

std::vector<raySegment> gazebo::sensors::RadiationSensor::CheckSourceViewable(ignition::math::Vector3d sensor_pos, ignition::math::Vector3d source_pos, std::string name)
{

  std::vector<raySegment> v;

  while (1)
  {
    std::string entityName = "";
    double blocking_dist;
    boost::recursive_mutex::scoped_lock lock(*(
        this->world->GetPhysicsEngine()->GetPhysicsUpdateMutex()));

    this->blockingRay->SetPoints(sensor_pos, source_pos);
    this->blockingRay->GetIntersection(blocking_dist, entityName);

    if (entityName == "")
    {
      return v;
    }
    else if (blocking_dist > (sensor_pos - source_pos).Length())
    {
      return v;
    }
    else if (entityName.find(name) != std::string::npos)
    {
      return v;
    }
    else
    {
      //entityName = entityName.substr(0, entityName.find("::"));
      if (v.empty())
      {
        v.push_back(raySegment(blocking_dist, std::string("free_space"), entityName));
      }
      else if (v.back().to == entityName)
      {
        v.push_back(raySegment(blocking_dist, entityName, std::string("free_space")));
      }
      else if (v.back().to == std::string("free_space"))
      {
        v.push_back(raySegment(blocking_dist, std::string("free_space"), entityName));
      }
      else
      {
        v.push_back(raySegment(blocking_dist, v.back().to, entityName));
      }
      ignition::math::Vector3d v1 = (source_pos - sensor_pos).Normalize() * (blocking_dist + 0.0001);
      ;
      sensor_pos = v1 + sensor_pos;
    }
  }
}

sdf::ElementPtr gazebo::sensors::RadiationSource::GetSDF()
{
  return this->sdf;
}
