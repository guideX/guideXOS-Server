namespace gxos {
namespace javascript {
namespace {

class PortableJsonSerializerImpl {
public:
    PortableJsonSerializerImpl(const Value& value,
        PortableObjectStore& objects, RuntimeStringStore& strings,
        PortableJsonOutputWriter& writer,
        PortableJsonSerializeStorageView storage,
        PortableJsonSerializeLimits limits,
        PortableJsonSerializeStepConsumer stepConsumer, void* stepContext,
        PortableJsonObjectValidator objectValidator,
        void* objectValidatorContext)
        : root_(value), objects_(objects), strings_(strings), writer_(writer),
          storage_(storage), limits_(limits), stepConsumer_(stepConsumer),
          stepContext_(stepContext), objectValidator_(objectValidator),
          objectValidatorContext_(objectValidatorContext)
    {
    }

    PortableJsonSerializeResult run()
    {
        writer_.reset();
        writer_.setLimit(limits_.maxOutputBytes);
        result_ = PortableJsonSerializeResult();
        if ((storage_.frames == nullptr && storage_.frameCapacity != 0u) ||
            (storage_.numberScratch == nullptr &&
                storage_.numberScratchCapacity != 0u) ||
            storage_.numberScratchCapacity == 0u) {
            setError(PortableJsonSerializeErrorCode::InvalidInput);
            return finishError();
        }
        if (limits_.maxSteps == 0u) {
            setError(PortableJsonSerializeErrorCode::StepBudgetExceeded);
            return finishError();
        }

        Value current = root_;
        bool valuePending = true;
        bool topLevelValue = true;

        while (true) {
            if (valuePending) {
                if (!consumeStep()) return finishError();
                switch (current.type()) {
                case ValueType::Undefined:
                case ValueType::Function:
                case ValueType::Symbol:
                    if (topLevelValue) return finishNoResult();
                    setError(PortableJsonSerializeErrorCode::UnsupportedValue);
                    return finishError();
                case ValueType::Null:
                    if (!appendLiteral("null")) return finishError();
                    valuePending = false;
                    break;
                case ValueType::Boolean:
                    if (!appendLiteral(current.booleanValue() ? "true"
                            : "false")) return finishError();
                    valuePending = false;
                    break;
                case ValueType::Number:
                    if (!emitNumber(current.numberValue()))
                        return finishError();
                    valuePending = false;
                    break;
                case ValueType::String:
                    if (!emitString(current)) return finishError();
                    valuePending = false;
                    break;
                case ValueType::Object:
                    if (!enterObject(current.objectId()))
                        return finishError();
                    valuePending = false;
                    topLevelValue = false;
                    break;
                case ValueType::HostObject:
                case ValueType::SymbolNamespace:
                case ValueType::ObjectNamespace:
                case ValueType::JsonNamespace:
                    setError(PortableJsonSerializeErrorCode::UnsupportedValue);
                    return finishError();
                }
                continue;
            }

            if (frameCount_ == 0u) {
                SourceView output;
                if (!writer_.complete(output)) {
                    readWriterError();
                    return finishError();
                }
                result_.status = PortableJsonSerializeStatus::Success;
                result_.output = output;
                result_.stats.stepsConsumed = stepsConsumed_;
                return result_;
            }

            PortableJsonTraversalFrame& frame =
                storage_.frames[frameCount_ - 1u];
            if (frame.array) {
                if (!advanceArray(frame, current, valuePending))
                    return finishError();
            } else if (!advanceObject(frame, current, valuePending)) {
                return finishError();
            }
        }
    }

private:
    bool consumeStep()
    {
        if (stepsConsumed_ >= limits_.maxSteps) {
            setError(PortableJsonSerializeErrorCode::StepBudgetExceeded);
            return false;
        }
        ++stepsConsumed_;
        if (stepConsumer_ != nullptr && !stepConsumer_(stepContext_)) {
            setError(PortableJsonSerializeErrorCode::StepBudgetExceeded);
            return false;
        }
        return true;
    }

    bool appendByte(char value)
    {
        if (writer_.appendByte(value)) return true;
        readWriterError();
        return false;
    }

    bool appendLiteral(const char* literal)
    {
        std::size_t length = 0u;
        while (literal[length] != '\0') ++length;
        if (writer_.appendSpan(literal, length)) return true;
        readWriterError();
        return false;
    }

    bool appendQuoted(SourceView text)
    {
        if (writer_.appendQuotedUtf8(text, &writerStep, this)) return true;
        if (error_.code == PortableJsonSerializeErrorCode::None)
            readWriterError();
        return false;
    }

    static bool writerStep(void* context)
    {
        PortableJsonSerializerImpl* serializer =
            static_cast<PortableJsonSerializerImpl*>(context);
        if (serializer == nullptr) return false;
        if (serializer->consumeStep()) return true;
        serializer->error_.writerError =
            PortableJsonWriteErrorCode::StepBudgetExceeded;
        return false;
    }

    void readWriterError()
    {
        error_.writerError = writer_.error();
        switch (writer_.error()) {
        case PortableJsonWriteErrorCode::None:
            setError(PortableJsonSerializeErrorCode::InternalInvariantFailure);
            break;
        case PortableJsonWriteErrorCode::CapacityExceeded:
            setError(PortableJsonSerializeErrorCode::OutputCapacityExceeded);
            break;
        case PortableJsonWriteErrorCode::InvalidUtf8:
            setError(PortableJsonSerializeErrorCode::InvalidStringState);
            break;
        case PortableJsonWriteErrorCode::InvalidBuffer:
            setError(PortableJsonSerializeErrorCode::InvalidInput);
            break;
        case PortableJsonWriteErrorCode::StepBudgetExceeded:
            setError(PortableJsonSerializeErrorCode::StepBudgetExceeded);
            break;
        }
    }

    bool emitNumber(double value)
    {
        std::size_t length = 0u;
        const PortableJsonNumberFormatCode code =
            formatPortableJsonBinary64(value, storage_.numberScratch,
                storage_.numberScratchCapacity, length);
        if (code == PortableJsonNumberFormatCode::NonFinite)
            return appendLiteral("null");
        if (code != PortableJsonNumberFormatCode::None) {
            error_.numberError = code;
            setError(PortableJsonSerializeErrorCode::NumericFormattingFailure);
            return false;
        }
        if (!writer_.appendSpan(storage_.numberScratch, length)) {
            readWriterError();
            return false;
        }
        return true;
    }

    bool emitString(const Value& value)
    {
        SourceView text;
        RuntimeStringError stringError;
        if (!strings_.view(value.stringId(), text, stringError)) {
            error_.stringError = stringError.code;
            setError(stringError.code == RuntimeStringErrorCode::StaleGeneration ||
                    stringError.code == RuntimeStringErrorCode::InvalidReference
                ? PortableJsonSerializeErrorCode::StaleStringHandle
                : PortableJsonSerializeErrorCode::InvalidStringState);
            return false;
        }
        return appendQuoted(text);
    }

    void setObjectError(const PortableObjectError& objectError)
    {
        error_.objectError = objectError.code;
        if (objectError.code == PortableObjectErrorCode::StaleGeneration ||
            objectError.code == PortableObjectErrorCode::InvalidObject) {
            setError(PortableJsonSerializeErrorCode::StaleObjectHandle);
        } else {
            setError(PortableJsonSerializeErrorCode::ObjectStoreFailure);
        }
    }

    bool enterObject(RuntimeObjectId object)
    {
        PortableObjectError objectError;
        if (!objects_.validObject(object, objectError)) {
            setObjectError(objectError);
            return false;
        }
        if (objectValidator_ != nullptr &&
            objectValidator_(objectValidatorContext_, object) ==
                PortableJsonObjectDisposition::HostedOperationRequired) {
            setError(PortableJsonSerializeErrorCode::HostedOperationRequired);
            return false;
        }

        Value toJson = Value::undefined();
        bool hasToJson = false;
        if (!objects_.getProperty(object, SourceView("toJSON", 6u), toJson,
                hasToJson, objectError)) {
            setObjectError(objectError);
            return false;
        }
        if (hasToJson && toJson.isFunction()) {
            setError(PortableJsonSerializeErrorCode::UnsupportedCallback);
            return false;
        }

        bool array = false;
        if (!objects_.isArray(object, array, objectError)) {
            setObjectError(objectError);
            return false;
        }
        if (frameCount_ >= limits_.maxNestingDepth) {
            setError(PortableJsonSerializeErrorCode::DepthLimitExceeded);
            return false;
        }
        if (frameCount_ >= storage_.frameCapacity ||
            storage_.frames == nullptr) {
            setError(PortableJsonSerializeErrorCode::TraversalCapacityExceeded);
            return false;
        }
        for (std::size_t index = 0u; index < frameCount_; ++index) {
            if (storage_.frames[index].object == object) {
                setError(PortableJsonSerializeErrorCode::CycleDetected);
                return false;
            }
        }

        PortableJsonTraversalFrame frame;
        frame.object = object;
        frame.array = array;
        if (array) {
            if (!objects_.arrayLength(object, frame.arrayLength, objectError)) {
                setObjectError(objectError);
                return false;
            }
            const std::size_t maximumLength =
                limits_.maxArrayLength < objects_.limits().maxArrayLength
                ? limits_.maxArrayLength : objects_.limits().maxArrayLength;
            if (frame.arrayLength > maximumLength) {
                setError(PortableJsonSerializeErrorCode::ArrayLimitExceeded);
                return false;
            }
            if (!appendByte('[')) return false;
        } else {
            if (!objects_.beginOwnStringPropertyIteration(object,
                    frame.propertyCursor, objectError)) {
                setObjectError(objectError);
                return false;
            }
            if (!appendByte('{')) return false;
        }
        storage_.frames[frameCount_++] = frame;
        ++result_.stats.containersEntered;
        if (frameCount_ > result_.stats.maximumDepth)
            result_.stats.maximumDepth = frameCount_;
        return true;
    }

    bool advanceArray(PortableJsonTraversalFrame& frame, Value& current,
        bool& valuePending)
    {
        if (frame.cursor >= frame.arrayLength) {
            if (!appendByte(']')) return false;
            popFrame();
            return true;
        }
        if (!consumeStep()) return false;
        const std::size_t index = frame.cursor++;
        ++result_.stats.arraySlotsVisited;
        if (index != 0u && !appendByte(',')) return false;

        char keyBytes[16] = {};
        std::size_t keyLength = 0u;
        std::size_t remaining = index;
        do {
            keyBytes[keyLength++] = static_cast<char>('0' + remaining % 10u);
            remaining /= 10u;
        } while (remaining != 0u);
        for (std::size_t left = 0u, right = keyLength - 1u;
                left < right; ++left, --right) {
            const char temporary = keyBytes[left];
            keyBytes[left] = keyBytes[right];
            keyBytes[right] = temporary;
        }

        bool found = false;
        PortableObjectError objectError;
        if (!objects_.getProperty(frame.object,
                SourceView(keyBytes, keyLength), current, found,
                objectError)) {
            setObjectError(objectError);
            return false;
        }
        if (isOmittable(current)) {
            if (!appendLiteral("null")) return false;
            valuePending = false;
            return true;
        }
        valuePending = true;
        return true;
    }

    bool advanceObject(PortableJsonTraversalFrame& frame, Value& current,
        bool& valuePending)
    {
        if (!consumeStep()) return false;
        PortablePropertyEntry property;
        bool found = false;
        std::size_t inspections = 0u;
        PortableObjectError objectError;
        if (!objects_.nextOwnStringProperty(frame.propertyCursor, property,
                found, inspections, objectError)) {
            setObjectError(objectError);
            return false;
        }
        result_.stats.propertyStorageInspections += inspections;
        if (!found) {
            if (!appendByte('}')) return false;
            popFrame();
            return true;
        }
        ++result_.stats.propertiesVisited;
        if (!validPropertyAttributes(property.attributes)) {
            setError(PortableJsonSerializeErrorCode::UnsupportedPropertyAttributes);
            return false;
        }
        if ((property.attributes & 0x02u) == 0u) return true;
        if (isOmittable(property.value)) return true;

        if (frame.emittedProperties != 0u && !appendByte(',')) return false;
        if (!appendQuoted(property.key) || !appendByte(':')) return false;
        ++frame.emittedProperties;
        current = property.value;
        valuePending = true;
        return true;
    }

    static bool isOmittable(const Value& value)
    {
        return value.isUndefined() || value.isFunction() || value.isSymbol();
    }

    static bool validPropertyAttributes(std::uint8_t attributes)
    {
        if ((attributes & static_cast<std::uint8_t>(~0x3fu)) != 0u ||
            attributes == 0u) return false;
        const std::uint8_t known = static_cast<std::uint8_t>(attributes & 0x38u);
        return known == 0u || known == 0x38u;
    }

    void popFrame()
    {
        if (frameCount_ != 0u)
            storage_.frames[--frameCount_] = PortableJsonTraversalFrame();
    }

    void clearFrames()
    {
        while (frameCount_ != 0u) popFrame();
    }

    void setError(PortableJsonSerializeErrorCode code)
    {
        if (error_.code == PortableJsonSerializeErrorCode::None)
            error_.code = code;
    }

    PortableJsonSerializeResult finishNoResult()
    {
        clearFrames();
        writer_.reset();
        result_.status = PortableJsonSerializeStatus::NoResult;
        result_.output = SourceView();
        result_.error.code = PortableJsonSerializeErrorCode::None;
        result_.stats.stepsConsumed = stepsConsumed_;
        return result_;
    }

    PortableJsonSerializeResult finishError()
    {
        if (error_.code == PortableJsonSerializeErrorCode::None)
            readWriterError();
        clearFrames();
        result_.status = PortableJsonSerializeStatus::Error;
        result_.error = error_;
        result_.output = SourceView();
        result_.stats.stepsConsumed = stepsConsumed_;
        writer_.reset();
        return result_;
    }

    const Value& root_;
    PortableObjectStore& objects_;
    RuntimeStringStore& strings_;
    PortableJsonOutputWriter& writer_;
    PortableJsonSerializeStorageView storage_;
    PortableJsonSerializeLimits limits_;
    PortableJsonSerializeStepConsumer stepConsumer_;
    void* stepContext_;
    PortableJsonObjectValidator objectValidator_;
    void* objectValidatorContext_;
    std::size_t frameCount_ = 0u;
    std::size_t stepsConsumed_ = 0u;
    PortableJsonSerializeError error_;
    PortableJsonSerializeResult result_;
};

} // namespace

inline PortableJsonSerializeResult serializePortableJson(
    const Value& value, PortableObjectStore& objects,
    RuntimeStringStore& strings, PortableJsonOutputWriter& writer,
    PortableJsonSerializeStorageView storage,
    PortableJsonSerializeLimits limits,
    PortableJsonSerializeStepConsumer stepConsumer, void* stepContext,
    PortableJsonObjectValidator objectValidator,
    void* objectValidatorContext)
{
    PortableJsonSerializerImpl serializer(value, objects, strings, writer,
        storage, limits, stepConsumer, stepContext, objectValidator,
        objectValidatorContext);
    return serializer.run();
}

inline const char* portableJsonSerializeErrorCodeName(
    PortableJsonSerializeErrorCode code)
{
    switch (code) {
    case PortableJsonSerializeErrorCode::None: return "None";
    case PortableJsonSerializeErrorCode::InvalidInput: return "InvalidInput";
    case PortableJsonSerializeErrorCode::InvalidValue: return "InvalidValue";
    case PortableJsonSerializeErrorCode::StaleObjectHandle:
        return "StaleObjectHandle";
    case PortableJsonSerializeErrorCode::StaleStringHandle:
        return "StaleStringHandle";
    case PortableJsonSerializeErrorCode::OutputCapacityExceeded:
        return "OutputCapacityExceeded";
    case PortableJsonSerializeErrorCode::ArrayLimitExceeded:
        return "ArrayLimitExceeded";
    case PortableJsonSerializeErrorCode::TraversalCapacityExceeded:
        return "TraversalCapacityExceeded";
    case PortableJsonSerializeErrorCode::DepthLimitExceeded:
        return "DepthLimitExceeded";
    case PortableJsonSerializeErrorCode::StepBudgetExceeded:
        return "StepBudgetExceeded";
    case PortableJsonSerializeErrorCode::CycleDetected: return "CycleDetected";
    case PortableJsonSerializeErrorCode::UnsupportedCallback:
        return "UnsupportedCallback";
    case PortableJsonSerializeErrorCode::HostedOperationRequired:
        return "HostedOperationRequired";
    case PortableJsonSerializeErrorCode::UnsupportedPropertyAttributes:
        return "UnsupportedPropertyAttributes";
    case PortableJsonSerializeErrorCode::UnsupportedValue:
        return "UnsupportedValue";
    case PortableJsonSerializeErrorCode::NumericFormattingFailure:
        return "NumericFormattingFailure";
    case PortableJsonSerializeErrorCode::InvalidStringState:
        return "InvalidStringState";
    case PortableJsonSerializeErrorCode::ObjectStoreFailure:
        return "ObjectStoreFailure";
    case PortableJsonSerializeErrorCode::InternalInvariantFailure:
        return "InternalInvariantFailure";
    }
    return "Unknown";
}

} // namespace javascript
} // namespace gxos
