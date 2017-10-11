#include "CoreImplementation.h"
#include "CoreInterface.h"

#include "AutogenMetadata.h"

#include "core/objects/MaterialObject.h"
#include "core/objects/GlobalObjectsHolder.h"
#include "core/objects/mobs/Mob.h"
#include "core/objects/mobs/LoginMob.h"
#include "core/Map.h"

#include "core/objects/test/UnsyncGenerator.h"
#include "core/objects/GlobalObjectsHolder.h"

#include "core/SynchronizedRandom.h"
#include "core/objects/PhysicsEngine.h"
#include "core/objects/Lobby.h"
#include "core/objects/SpawnPoints.h"

#include "core/atmos/Atmos.h"
#include "core/ObjectFactory.h"
#include "core/Names.h"

namespace
{

kv::CoreInterface::ObjectsMetadata GenerateMetadata()
{
    kv::CoreInterface::ObjectsMetadata retval;

    for (auto it : (*GetItemsCreators()))
    {
        std::unique_ptr<kv::Object> object(it.second());
        kv::MaterialObject* material = CastTo<kv::MaterialObject>(object.get());
        if (!material)
        {
            qDebug() << QString("Type '%1' is not material object!").arg(it.first);
            continue;
        }

        kv::CoreInterface::ObjectMetadata metadata;
        metadata.name = it.first;
        metadata.default_view = *(material->GetView());

        retval.insert(metadata.name, metadata);
    }

    return retval;
}

}

namespace kv
{

WorldImplementation::WorldImplementation()
    : atmos_(new Atmosphere),
      factory_(new ObjectFactory(this)),
      names_(new Names(this)),
      process_messages_ns_(0),
      foreach_process_ns_(0),
      force_process_ns_(0),
      atmos_process_ns_(0),
      deletion_process_ns_(0),
      update_visibility_ns_(0),
      frame_generation_ns_(0)
{
    // Nothing
}

WorldImplementation::~WorldImplementation()
{
    // Nothing
}

void WorldImplementation::SaveWorld(FastSerializer* data) const
{
    WorldLoaderSaver::Save(this, *data);
}

// TODO: Look into #360 properly
void WorldImplementation::ProcessNextTick(const QVector<Message>& messages)
{
    RemoveStaleRepresentation();

    ProcessInputMessages(messages);

    // TODO
    Q_UNUSED(messages)
}

void WorldImplementation::RemoveStaleRepresentation()
{
    sounds_for_frame_.clear();
    chat_frame_info_.Reset();
}

void WorldImplementation::ProcessInputMessages(const QVector<Message>& messages)
{
    for (const Message& message : qAsConst(messages))
    {
        ProcessInputMessage(message);
    }
}

void WorldImplementation::ProcessInputMessage(const Message& message)
{
    if (message.type == MessageType::NEW_CLIENT)
    {
        const int new_id = message.data.value("id").toInt();

        const quint32 player_id = GetPlayerId(new_id);
        if (player_id != 0)
        {
            qDebug() << "Client under net_id" << player_id << "already exists";
            return;
        }

        IdPtr<LoginMob> mob = GetFactory().CreateImpl(LoginMob::GetTypeStatic());
        SetPlayerId(player_id,mob.Id());
        mob->MindEnter();

        qDebug() << "New client: " << player_id << mob.Id();
        return;
    }
    if (message.type == MessageType::OOC_MESSAGE)
    {
        const QString login = message.data["login"].toString();
        const QString text = message.data["text"].toString();
        PostOoc(login, text);
        return;
    }
    // TODO: more processing
}

void WorldImplementation::PostOoc(const QString& who, const QString& text)
{
    const QString escaped_who = who.toHtmlEscaped();
    const QString escaped_text = text.toHtmlEscaped();

    const QString post_text
        = QString("<font color=\"blue\"><b>%1</b>: <span>%2</span></font>")
            .arg(escaped_who)
            .arg(escaped_text);

    const auto& players = GetGlobals()->players_table;
    for (auto it = players.begin(); it != players.end(); ++it)
    {
        GetChatFrameInfo().PostPersonal(post_text, it.key());
    }
}

void WorldImplementation::Represent(const QVector<PlayerAndFrame>& frames) const
{
    for (const PlayerAndFrame player_and_frame : frames)
    {
        const quint32 player_net_id = player_and_frame.first;
        GrowingFrame* frame = player_and_frame.second;

        AppendSystemTexts(frame);

        VisiblePoints points;
        GetMob()->CalculateVisible(&points);

        GetMap().Represent(frame, points);

        if (IdPtr<Mob> mob = GetPlayerId(player_net_id))
        {
            mob->GenerateInterfaceForFrame(frame);
        }
        else
        {
            qDebug() << "Oops! No mob for such net id!" << player_net_id;
        }

        GetAtmosphere().Represent(frame);

        AppendSoundsToFrame(frame, points, player_net_id);
        AppendChatMessages(frame, points, player_net_id);

        // TODO: reset all shifts
        frame->SetCamera(GetMob()->GetPosition().x, GetMob()->GetPosition().y);
    }
}

void WorldImplementation::AppendSystemTexts(GrowingFrame* frame) const
{
    auto append = [frame](QString tab, QString text, auto value)
    {
        frame->Append(FrameData::TextEntry{tab, text.arg(value)});
    };
    auto append_ns = [&append](QString text, qint64 ns)
    {
        append("Performance", text, (ns * 1.0) / 1000000.0);
    };

    append("Main", "Game tick: %1", GetGlobals()->game_tick);

    append_ns("Process messages: %1 ms", process_messages_ns_);
    append_ns("Process objects: %1 ms", foreach_process_ns_);
    append_ns("Process force movement: %1 ms", force_process_ns_);
    append_ns("Process atmos: %1 ms", atmos_process_ns_);
    append_ns("Process deletion: %1 ms", deletion_process_ns_);
    append_ns("Update visibility: %1 ms", update_visibility_ns_);
    append_ns("Frame generation: %1 ms", frame_generation_ns_);
}

void WorldImplementation::AppendSoundsToFrame(
    GrowingFrame* frame, const VisiblePoints& points, quint32 net_id) const
{
    // TODO: Sounds for frame may be hash table or some sorted vector
    // now scalability is quite bad
    for (auto it : qAsConst(sounds_for_frame_))
    {
        if (std::find(points.begin(), points.end(), it.first) != points.end())
        {
            frame->Append(FrameData::Sound{it.second});
        }
    }

    auto& musics_for_mobs = global_objects_->musics_for_mobs;

    auto music = musics_for_mobs.find(net_id);
    if (music != musics_for_mobs.end())
    {
        FrameData::Music frame_music;
        frame_music.name = music->first;
        frame_music.volume = music->second;
        frame->SetMusic(frame_music);
    }
}

void WorldImplementation::AppendChatMessages(
    GrowingFrame* frame, const VisiblePoints& points, quint32 net_id) const
{
    auto& visible = GetChatFrameInfo().GetVisible();
    for (const auto& point : points)
    {
        for (const auto& text : visible[point])
        {
            frame->Append(FrameData::ChatMessage{text});
        }
    }

    for (const auto& personal : chat_frame_info_.GetPersonalTexts(net_id))
    {
        frame->Append(FrameData::ChatMessage{personal});
    }
}

quint32 WorldImplementation::Hash() const
{
    return factory_->Hash();
}

AtmosInterface& WorldImplementation::GetAtmosphere()
{
    return *atmos_;
}

const AtmosInterface& WorldImplementation::GetAtmosphere() const
{
    return *atmos_;
}

MapInterface& WorldImplementation::GetMap()
{
    return *(global_objects_->map);
}

const MapInterface& WorldImplementation::GetMap() const
{
    return *(global_objects_->map);
}

ObjectFactoryInterface& WorldImplementation::GetFactory()
{
    return *factory_;
}

const ObjectFactoryInterface& WorldImplementation::GetFactory() const
{
    return *factory_;
}

Names& WorldImplementation::GetNames()
{
    return *names_;
}

ChatFrameInfo& WorldImplementation::GetChatFrameInfo()
{
    return chat_frame_info_;
}

const ChatFrameInfo& WorldImplementation::GetChatFrameInfo() const
{
    return chat_frame_info_;
}

IdPtr<Mob> WorldImplementation::GetMob() const
{
    return current_mob_;
}

void WorldImplementation::SetMob(quint32 new_mob)
{
    current_mob_ = new_mob;
}

IdPtr<GlobalObjectsHolder> WorldImplementation::GetGlobals() const
{
    return global_objects_;
}

void WorldImplementation::SetGlobals(quint32 globals)
{
    global_objects_ = globals;
}

void WorldImplementation::SetPlayerId(quint32 net_id, quint32 real_id)
{
    global_objects_->players_table[net_id] = real_id;
}

quint32 WorldImplementation::GetPlayerId(quint32 net_id) const
{
    const auto& players_table = global_objects_->players_table;
    const auto it = players_table.find(net_id);
    if (it != players_table.end())
    {
        return it.value();
    }
    return 0;
}

quint32 WorldImplementation::GetNetId(quint32 real_id) const
{
    const auto& players_table = global_objects_->players_table;
    for (auto it = players_table.begin(); it != players_table.end(); ++it)
    {
        if (it.value() == real_id)
        {
            return it.key();
        }
    }
    return 0;
}

void WorldImplementation::AddSound(const QString& name, const Position position)
{
    sounds_for_frame_.append({position, name});
}

void WorldImplementation::PlayMusic(const QString& name, const int volume, const quint32 mob)
{
    qDebug() << "Music playing:" << mob << name << volume;
    auto& musics_for_mobs = global_objects_->musics_for_mobs;
    musics_for_mobs[mob] = {name, volume};
}

void WorldImplementation::PrepareToMapgen()
{
    qsrand(QDateTime::currentDateTime().toMSecsSinceEpoch());

    global_objects_ = GetFactory().CreateImpl(kv::GlobalObjectsHolder::GetTypeStatic());
    global_objects_->map = GetFactory().CreateImpl(kv::Map::GetTypeStatic());
    global_objects_->random = GetFactory().CreateImpl(kv::SynchronizedRandom::GetTypeStatic());
    global_objects_->physics_engine_ = GetFactory().CreateImpl(kv::PhysicsEngine::GetTypeStatic());

    quint32 seed = static_cast<quint32>(qrand());
    global_objects_->random->SetParams(seed, 0);
}

void WorldImplementation::AfterMapgen(const quint32 id, const bool unsync_generation)
{
    global_objects_->lobby = GetFactory().CreateImpl(kv::Lobby::GetTypeStatic());

    if (unsync_generation)
    {
        global_objects_->unsync_generator
            = GetFactory().CreateImpl(UnsyncGenerator::GetTypeStatic());
    }

    for (auto it = GetFactory().GetIdTable().begin();
              it != GetFactory().GetIdTable().end();
            ++it)
    {
        if (it->object && (it->object->GetTypeIndex() == SpawnPoint::GetTypeIndexStatic()))
        {
            global_objects_->lobby->AddSpawnPoint(it->object->GetId());
        }
    }

    IdPtr<LoginMob> newmob = GetFactory().CreateImpl(LoginMob::GetTypeStatic());

    SetPlayerId(id, newmob.Id());
    SetMob(newmob.Id());
    newmob->MindEnter();
}

CoreImplementation::WorldPtr CoreImplementation::CreateWorldFromSave(
    const QByteArray& data, const quint32 mob_id)
{
    auto world = std::make_shared<WorldImplementation>();
    FastDeserializer deserializer(data.data(), data.size());
    WorldLoaderSaver::Load(world.get(), deserializer, mob_id);
    return world;
}

CoreImplementation::WorldPtr CoreImplementation::CreateWorldFromMapgen(
    const QByteArray& data, const quint32 mob_id, const Config& config)
{
    auto world = std::make_shared<WorldImplementation>();

    world->PrepareToMapgen();

    FastDeserializer deserializer(data.data(), data.size());
    WorldLoaderSaver::LoadFromMapGen(world.get(), deserializer);

    world->AfterMapgen(mob_id, config.unsync_generation);

    return world;
}

const CoreImplementation::ObjectsMetadata& CoreImplementation::GetObjectsMetadata() const
{
    static const ObjectsMetadata metadata = GenerateMetadata();
    return metadata;
}

CoreInterface& GetCoreInstance()
{
    static CoreImplementation core;
    return core;
}

}