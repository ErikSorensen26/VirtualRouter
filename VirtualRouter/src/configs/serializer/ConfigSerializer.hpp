/**
 * @file ConfigSerializer.hpp
 * @brief Walks a live registry tree and writes it back out as CLI text.
 * @ingroup CONFIG
 *
 * The inverse of the CLI executor: where @ref cli::execution::Executor turns a
 * parsed line into a config write, this turns the config back into lines that
 * would produce it. It drives off the same generic field-visitation machinery
 * the grammar generator and the executor already use -- @c SubRegistry::visit,
 * @ref config::visitField, @c RegistryTable.hpp's registry list -- so it needs
 * no per-protocol code: adding a registry to REGISTRY_ID_LIST is enough for its
 * fields to serialize.
 *
 * ## What gets printed
 * Only fields whose accessor reports @c overridden() (state == CANNED): an
 * inherited or still-default field is exactly what running-config omits on a
 * real router, and printing every field's default would make the output
 * useless as a diff or a record of intent. A field's canonical spelling comes
 * from @ref CommandPathIndex, not from the node that actually wrote it, so two
 * sessions that configured the same thing two different ways serialize
 * identically -- see the CommandPath.hpp doc for why.
 *
 * ## What is not yet handled
 * - @c ListField / @c LIST_FIELD_CB tuple lists (OSPF area ranges, networks,
 *   summary addresses): each vector entry needs one line built from its
 *   tuple's members in declared order. Left as a follow-up; the per-field
 *   dispatch below already has the hook point (@ref serializeListField).
 * - A scalar field (ValueField, AtomicField or OptionalAtomicField) whose
 *   stored value is itself a tuple schema (distribute-list, table-map,
 *   OspfQueueDepth): same shape as a list field's element, minus the vector.
 *   Detected and skipped rather than misprinted -- see the isStdTupleV /
 *   hasTupleSchemaV check in serializeScalar.
 * - Deferred/resolver fields (see grammar-variable-args): the field's value
 *   was supplied by whichever command resolved it, not the one that named the
 *   key, so its canonical path is not simply "the node bound to this configId".
 *
 * An @c InterfaceKey (an owned-list key) does print, via @c formatValue --
 * its two CLI tokens ("GigabitEthernet", "0/1") joined by a space, since the
 * one-token-per-line-element model here has no problem with a "word" that
 * happens to contain a space in the printed line.
 */

#if 0
#ifndef CONFIG_SERIALIZER_CONFIG_SERIALIZER_HPP
#define CONFIG_SERIALIZER_CONFIG_SERIALIZER_HPP

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include <EnumBitMap.hpp>

#include "configs/RegistryTable.hpp"
#include "configs/FieldAccessor.hpp"
#include "configs/TupleSchema.hpp"
#include "configs/serializer/CommandPath.hpp"
#include "configs/serializer/ConfigWriter.hpp"
#include "configs/serializer/ValueFormat.hpp"

namespace config::serializer
{
/// @brief True for a value stored as a raw std::tuple, i.e. a ValueField's unwrapped storage.
template <typename T>
inline constexpr bool isStdTupleV = false;
template <typename... Ts>
inline constexpr bool isStdTupleV<std::tuple<Ts...>> = true;

/// @brief True when @p T needs TupleSchema/tuple handling this serializer does not yet do,
/// covering both an AtomicField/OptionalAtomicField storing the schema type directly and a
/// ValueField storing its StorageOf<T> unwrap -- and, as a safe fallback, any value @c
/// formatValue has no overload for, e.g. the registry's own `nullptr_t` TODO placeholders.
template <typename T>
concept SerializableScalar = requires(const T& v) { formatValue(v); };

/**
 * @brief Drives the walk over one registry instance and its children.
 *
 * Stateless apart from the index and writer it was constructed with, so one
 * instance can serialize as many registry trees as asked -- the global
 * registry, then each VRF's, with nothing to reset between them.
 */
class ConfigSerializer
{
public:
    ConfigSerializer(const CommandPathIndex& index, ConfigWriter& writer)
        : index(index), writer(writer)
    {}

    /**
     * @brief Serializes every overridden field of @p reg, recursing into child scopes.
     *
     * @tparam ENUM Field enum of @p reg's registry type; drives which
     *              REGISTRY_ID_LIST entry supplies configId's registry half.
     */
    template <typename ENUM, typename Registry>
    void serialize(Registry& reg)
    {
        constexpr uint16_t regId = config::registryIdV<ENUM>;
        constexpr size_t count = config::registrySlotsV<ENUM>;

        for (size_t i = 0; i < count; ++i)
        {
            reg.visit(i, [&](auto&& accessor)
            {
                serializeField<ENUM>(regId, static_cast<uint32_t>(i), accessor);
            });
        }
    }

private:
    template <typename ENUM, typename Accessor>
    void serializeField(uint16_t regId, uint32_t fieldIdx, Accessor&& accessor)
    {
        using Visited = std::remove_cvref_t<Accessor>;
        const uint32_t configId = cli::tree::CommandNode::packConfig(regId, fieldIdx);

        if constexpr (config::IsRefContainer<Visited>)
        {
            // A REGISTRY_CONTAINER is always live; there is no "unconfigured"
            // state to test, so it recurses unconditionally. Its own fields
            // decide what, if anything, actually prints underneath it.
            recurseContainer(accessor);
        }
        else if constexpr (requires { typename Visited::Field; })
        {
            using Field = typename Visited::Field;

            if constexpr (config::IsAtomicField<Field> || config::IsOptionalAtomicField<Field>
                          || config::IsValueField<Field>)
            {
                serializeScalar<ENUM>(configId, accessor);
            }
            else if constexpr (config::IsListField<Field>)
            {
                serializeListField<ENUM>(configId, accessor);
            }
            else if constexpr (config::IsOwnedListField<Field>)
            {
                serializeOwnedList<ENUM>(configId, accessor);
            }
        }
    }

    /// @brief A container field's registry is entered unconditionally; its fields gate themselves.
    template <typename Accessor>
    void recurseContainer(Accessor& accessor)
    {
        using Registry = std::remove_cvref_t<decltype(accessor)>;
        using ChildEnum = typename Registry::type;
        if constexpr (config::isRegisteredV<ChildEnum>)
            serialize<ChildEnum>(accessor);
    }

    /**
     * @brief Prints one line for a scalar field that has been explicitly set.
     *
     * A bitmap-valued atomic field is a run of member keywords rather than one
     * value, so it is split out before the generic "path + value" case below.
     */
    template <typename ENUM, typename Accessor>
    void serializeScalar(uint32_t configId, Accessor& accessor)
    {
        if (!accessor.overridden()) return;

        using Value = std::remove_cvref_t<decltype(accessor.load())>;

        if constexpr (types::isEnumBitMapV<Value>)
        {
            serializeBitMap(configId, accessor.load());
        }
        else if constexpr (isStdTupleV<Value> || config::hasTupleSchemaV<Value>)
        {
            // A field whose stored value is a tuple schema (distribute-list,
            // table-map, OspfQueueDepth, ...), either as a ValueField's
            // unwrapped std::tuple or an Atomic/OptionalAtomicField storing
            // the schema type directly: needs its members named via
            // TupleSchema the way a ListField's elements do. Not yet
            // implemented -- see file doc.
        }
        else if constexpr (!SerializableScalar<Value>)
        {
            // No formatValue overload exists for this type -- a registry's
            // own placeholder type (e.g. the `nullptr_t` TODO fields) rather
            // than a real scalar. Nothing to print until it is given a real
            // type and a formatValue overload.
        }
        else
        {
            CommandWords words = index.lookup(configId);
            if (words.words.empty()) return;

            std::vector<std::string> line(words.words.begin(), words.words.end());

            if constexpr (std::is_same_v<Value, bool>)
            {
                // A bool field's path already spells the "on" keyword (e.g.
                // "shutdown"); a false value is what `no <path>` would set, so
                // the negation keyword is prepended rather than appending
                // "false".
                if (!accessor.load())
                    line.insert(line.begin(), "no");
            }
            else if (words.node.node().hasEnumChange())
            {
                // The path's leaf word already names this exact member;
                // nothing to append, unlike a scalar value which still needs
                // its own text.
            }
            else
            {
                line.push_back(formatValue(accessor.load()));
            }

            writer.line(line);
        }
    }

    /// @brief One flag keyword per set bit, joined onto the one line that sets them together.
    template <typename BitMap>
    void serializeBitMap(uint32_t configId, BitMap bits)
    {
        using Enum = typename BitMap::Enum;
        std::vector<std::string> line;

        for (size_t i = 0; i < static_cast<size_t>(Enum::COUNT); ++i)
        {
            if (!bits.test(static_cast<Enum>(i))) continue;

            CommandWords words = index.lookupEnumMember(configId, static_cast<uint16_t>(i));
            if (words.words.empty()) continue;

            if (line.empty())
                line.assign(words.words.begin(), words.words.end());
            else
                line.emplace_back(words.words.back());
        }

        if (!line.empty()) writer.line(line);
    }

    /**
     * @brief Placeholder for tuple-list fields (OSPF ranges, networks, ...).
     *
     * Each stored entry needs one line built from its tuple's members in
     * declared order via TupleSchema; not yet implemented. See the file doc.
     */
    template <typename ENUM, typename Accessor>
    void serializeListField(uint32_t /*configId*/, Accessor& /*accessor*/)
    {
    }

    /**
     * @brief Recurses into every child of an owned-list field (areas, neighbors, VRFs, ...).
     *
     * Each child is its own registry instance and prints as a nested scope:
     * the key names the mode line ("router ospf 1"), and the child's own
     * fields are indented one level under it via ConfigWriter::Scope.
     */
    template <typename ENUM, typename Accessor>
    void serializeOwnedList(uint32_t configId, Accessor& accessor)
    {
        using Field = typename Accessor::Field;
        using ChildRegistry = typename Field::type;
        using ChildEnum = typename ChildRegistry::type;

        if constexpr (!config::isRegisteredV<ChildEnum>) return;

        CommandWords words = index.lookup(configId);
        if (words.words.empty()) return;

        for (auto& [key, child] : accessor.get())
        {
            std::vector<std::string> line(words.words.begin(), words.words.end());
            line.push_back(keyToString(key));
            writer.line(line);

            ConfigWriter::Scope s = writer.scope();
            serialize<ChildEnum>(*child);
        }
    }

    /// @brief formatValue already covers strings, integers and every key type below; kept as
    /// its own name since a key is conceptually different from a field's value even though
    /// today it dispatches the same way.
    template <typename K>
    static std::string keyToString(const K& k)
    {
        return formatValue(k);
    }

    const CommandPathIndex& index;
    ConfigWriter& writer;
};
}

#endif // CONFIG_SERIALIZER_CONFIG_SERIALIZER_HPP
#endif
