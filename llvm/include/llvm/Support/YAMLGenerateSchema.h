//===- llvm/Support/YAMLGenerateSchema.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAMLGENERATE_SCHEMA_H
#define LLVM_SUPPORT_YAMLGENERATE_SCHEMA_H

#include "llvm/Support/YAMLTraits.h"


namespace llvm {

namespace json {
class Value;
}

namespace yaml {

class GenerateSchema : public IO {
public:
  GenerateSchema(raw_ostream &RO);
  ~GenerateSchema() override = default;

  IOKind getKind() const override;
  bool outputting() const override;
  bool mapTag(StringRef, bool) override;
  void beginMapping() override;
  void endMapping() override;
  bool preflightKey(const char *key, bool, bool, bool &, void *&) override;
  void postflightKey(void *) override;
  std::vector<StringRef> keys() override;
  void beginFlowMapping() override;
  void endFlowMapping() override;
  unsigned beginSequence() override;
  void endSequence() override;
  bool preflightElement(unsigned, void *&) override;
  void postflightElement(void *) override;
  unsigned beginFlowSequence() override;
  bool preflightFlowElement(unsigned, void *&) override;
  void postflightFlowElement(void *) override;
  void endFlowSequence() override;
  void beginEnumScalar() override;
  bool matchEnumScalar(const char *, bool) override;
  bool matchEnumFallback() override;
  void endEnumScalar() override;
  bool beginBitSetScalar(bool &) override;
  bool bitSetMatch(const char *, bool) override;
  void endBitSetScalar() override;
  void scalarString(StringRef &, QuotingType) override;
  void blockScalarString(StringRef &) override;
  void scalarTag(std::string &) override;
  NodeKind getNodeKind() override;
  void setError(const Twine &message) override;
  std::error_code error() override;
  bool canElideEmptySequence() override;

  // These are only used by operator<<. They could be private
  // if that templated operator could be made a friend.
  bool preflightDocument(unsigned);
  void postflightDocument();

  enum class PropertyKind : uint8_t {
    Properties,
    Required,
    Type,
    Enum,
    Items,
  };

  class SchemaNode {
  public:
    virtual json::Value toJSON() const = 0;

    virtual ~SchemaNode() = default;
  };

  class Schema;

  class SchemaProperty : public SchemaNode {
    PropertyKind Kind;
    StringRef Name;

  public:
    SchemaProperty(PropertyKind Kind, StringRef Name)
        : Kind(Kind), Name(Name) {}

    PropertyKind getKind() const { return Kind; }

    StringRef getName() const { return Name; }
  };

  class PropertiesProperty final
      : public SchemaProperty,
        SmallVector<std::pair<StringRef, Schema *>, 8> {
  public:
    using BaseVector = SmallVector<std::pair<StringRef, Schema *>, 8>;

    PropertiesProperty()
        : SchemaProperty(PropertyKind::Properties, "properties") {}

    using BaseVector::begin;
    using BaseVector::emplace_back;
    using BaseVector::end;
    using BaseVector::size;

    json::Value toJSON() const override;
  };

  using StringVector = SmallVector<StringRef, 4>;

  class RequiredProperty final : public SchemaProperty, StringVector {
  public:
    using BaseVector = StringVector;

    RequiredProperty() : SchemaProperty(PropertyKind::Required, "required") {}

    using BaseVector::begin;
    using BaseVector::emplace_back;
    using BaseVector::end;
    using BaseVector::size;

    json::Value toJSON() const override;
  };

  class TypeProperty final : public SchemaProperty {
    StringRef Value = "any";

  public:
    TypeProperty() : SchemaProperty(PropertyKind::Type, "type") {}

    StringRef getValue() const { return Value; }

    void setValue(StringRef Val) { Value = Val; }

    json::Value toJSON() const override;
  };

  class EnumProperty final : public SchemaProperty, StringVector {
  public:
    using BaseVector = StringVector;

    EnumProperty() : SchemaProperty(PropertyKind::Enum, "enum") {}

    using BaseVector::begin;
    using BaseVector::emplace_back;
    using BaseVector::end;
    using BaseVector::size;

    json::Value toJSON() const override;
  };

  class ItemsProperty final : public SchemaProperty {
    Schema* Value;

  public:
    ItemsProperty() : SchemaProperty(PropertyKind::Items, "items") {}

    Schema* getValue() const { return Value; }

    void setValue(Schema* Val) { Value = Val; }

    json::Value toJSON() const override;
  };

  class Schema final : public SchemaNode, SmallVector<SchemaProperty *, 8> {
  public:
    using BaseVector = SmallVector<SchemaProperty *, 8>;

    Schema() = default;

    using BaseVector::begin;
    using BaseVector::emplace_back;
    using BaseVector::end;
    using BaseVector::size;

    template <typename PropertyType> PropertyType *findProperty() const {
      auto P = PropertyType{};
      auto Found = std::find_if(begin(), end(), [&](auto &&Prop) {
        return Prop->getKind() == P.getKind();
      });
      return Found != end() ? static_cast<PropertyType *>(*Found) : nullptr;
    }

    template <typename PropertyType> bool hasProperty() const {
      return findProperty<PropertyType>() != nullptr;
    }

    json::Value toJSON() const override;
  };

private:
  std::vector<std::unique_ptr<SchemaNode>> SchemaNodes;
  SmallVector<Schema *, 8> Schemas;
  raw_ostream &RO;
  SchemaNode *Root = nullptr;

  template <typename PropertyType> PropertyType *createProperty() {
    auto UPtr = std::make_unique<PropertyType>();
    auto *Ptr = UPtr.get();
    SchemaNodes.emplace_back(std::move(UPtr));
    return Ptr;
  }

public:
  template <typename PropertyType>
  PropertyType *getOrCreateProperty(Schema &S) {
    auto* Found = S.findProperty<PropertyType>();
    if (!Found) {
      Found = createProperty<PropertyType>();
      S.emplace_back(Found);
    }
    return Found;
  }

  Schema *createSchema() {
    auto UPtr = std::make_unique<Schema>();
    auto *Ptr = UPtr.get();
    SchemaNodes.emplace_back(std::move(UPtr));
    return Ptr;
  }

  Schema *getTopSchema() const {
    return Schemas.empty() ? nullptr : Schemas.back();
  }
};

// Define non-member operator<< so that Output can stream out document list.
template <typename T>
inline std::enable_if_t<has_DocumentListTraits<T>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &DocList) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, DocumentListTraits<T>::element(Gen, DocList, 0), true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Define non-member operator<< so that Output can stream out a map.
template <typename T>
inline std::enable_if_t<has_MappingTraits<T, EmptyContext>::value,
                        GenerateSchema &>
operator<<(GenerateSchema &Gen, T &Map) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, Map, true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Define non-member operator<< so that Output can stream out a sequence.
template <typename T>
inline std::enable_if_t<has_SequenceTraits<T>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &Seq) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, Seq, true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Define non-member operator<< so that Output can stream out a block scalar.
template <typename T>
inline std::enable_if_t<has_BlockScalarTraits<T>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &Val) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, Val, true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Define non-member operator<< so that Output can stream out a string map.
template <typename T>
inline std::enable_if_t<has_CustomMappingTraits<T>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &Val) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, Val, true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Define non-member operator<< so that Output can stream out a polymorphic
// type.
template <typename T>
inline std::enable_if_t<has_PolymorphicTraits<T>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &Val) {
  EmptyContext Ctx;
  Gen.preflightDocument();
  yamlize(Gen, Val, true, Ctx);
  Gen.postflightDocument();
  return Gen;
}

// Provide better error message about types missing a trait specialization
template <typename T>
inline std::enable_if_t<missingTraits<T, EmptyContext>::value, GenerateSchema &>
operator<<(GenerateSchema &Gen, T &seq) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
  return Gen;
}

} // namespace yaml

} // namespace llvm

#endif // LLVM_SUPPORT_YAMLGENERATE_SCHEMA_H
