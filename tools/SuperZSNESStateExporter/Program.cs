using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Formatters.Binary;
using System.Security.Cryptography;
using System.Text;

namespace SuperZSNESStateExporter
{
    internal static class Program
    {
        private const int WramSize = 131072;
        private const int SpcRamSize = 65536;
        private const int CgramSize = 512;
        private const int OamSize = 544;
        private const int VramSize = 65536;
        private const int IoSize = 16384;
        private const int ExpectedRawTailSize =
            WramSize + SpcRamSize + CgramSize + OamSize + VramSize + IoSize + CgramSize + OamSize;

        private static readonly string[] ExpectedTypes =
        {
            "MasterExecutor+SaveStateMaster",
            "CPU65c816+SaveState65816",
            "CPUSPC700+SaveStateSPC700",
            "SNESPPU+PPUParams",
            "DSPAudio+SaveStateData"
        };

        private static int Main(string[] args)
        {
            try
            {
                Options options = Options.Parse(args);
                if (options.ShowHelp)
                {
                    Console.WriteLine(Options.Usage);
                    return 0;
                }

                Export(options);
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("SuperZSNES state export failed: " + ex);
                return 1;
            }
        }

        private static void Export(Options options)
        {
            string input = Path.GetFullPath(options.InputPath);
            string managed = Path.GetFullPath(options.ManagedDirectory);
            string output = Path.GetFullPath(options.OutputDirectory);
            if (!File.Exists(input))
                throw new FileNotFoundException("State file not found", input);
            if (!File.Exists(Path.Combine(managed, "Assembly-CSharp.dll")))
                throw new FileNotFoundException("Assembly-CSharp.dll was not found in the managed directory", managed);

            byte[] source = File.ReadAllBytes(input);
            if (source.Length < ExpectedRawTailSize || source.Length > 64 * 1024 * 1024)
                throw new InvalidDataException("State size is outside the supported bounds: " + source.Length);

            Assembly applicationAssembly = null;
            Dictionary<string, Assembly> dependencyCache = new Dictionary<string, Assembly>(StringComparer.OrdinalIgnoreCase);
            ResolveEventHandler resolver = delegate(object sender, ResolveEventArgs eventArgs)
            {
                string simpleName = new AssemblyName(eventArgs.Name).Name;
                if (string.Equals(simpleName, "Assembly-CSharp", StringComparison.OrdinalIgnoreCase) &&
                    applicationAssembly != null)
                    return applicationAssembly;
                if (dependencyCache.TryGetValue(simpleName, out Assembly cached))
                    return cached;
                string candidate = Path.Combine(managed, simpleName + ".dll");
                if (!File.Exists(candidate))
                    return null;
                Assembly loaded = Assembly.Load(File.ReadAllBytes(candidate));
                dependencyCache[simpleName] = loaded;
                return loaded;
            };
            AppDomain.CurrentDomain.AssemblyResolve += resolver;
            applicationAssembly = Assembly.Load(File.ReadAllBytes(Path.Combine(managed, "Assembly-CSharp.dll")));

            string temporary = output + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                Directory.CreateDirectory(temporary);
                object[] states = new object[ExpectedTypes.Length];
                long objectBytes;
                byte[][] raw = new byte[8][];
                using (MemoryStream stream = new MemoryStream(source, writable: false))
                {
                    BinaryFormatter formatter = new BinaryFormatter
                    {
                        Binder = new StateTypeBinder(applicationAssembly)
                    };
                    for (int i = 0; i < states.Length; i++)
                    {
                        states[i] = formatter.Deserialize(stream);
                        string actual = states[i].GetType().FullName;
                        if (!string.Equals(actual, ExpectedTypes[i], StringComparison.Ordinal))
                            throw new InvalidDataException(
                                "Object " + i + " has type " + actual + "; expected " + ExpectedTypes[i]);
                    }

                    objectBytes = stream.Position;
                    long remaining = stream.Length - stream.Position;
                    if (remaining != ExpectedRawTailSize)
                        throw new InvalidDataException(
                            "Unsupported tail size " + remaining + "; DKC v0.230 states require exactly " +
                            ExpectedRawTailSize + " raw bytes after the five state objects");

                    raw[0] = ReadExact(stream, WramSize);
                    raw[1] = ReadExact(stream, SpcRamSize);
                    raw[2] = ReadExact(stream, CgramSize);
                    raw[3] = ReadExact(stream, OamSize);
                    raw[4] = ReadExact(stream, VramSize);
                    raw[5] = ReadExact(stream, IoSize);
                    raw[6] = ReadExact(stream, CgramSize);
                    raw[7] = ReadExact(stream, OamSize);
                    if (stream.Position != stream.Length)
                        throw new InvalidDataException("State contains unconsumed data");
                }

                string[] rawNames =
                {
                    "wram.bin", "spc-ram.bin", "cgram.bin", "oam.bin", "vram.bin",
                    "io-registers.bin", "cgram-frame-start.bin", "oam-frame-start.bin"
                };
                for (int i = 0; i < raw.Length; i++)
                    File.WriteAllBytes(Path.Combine(temporary, rawNames[i]), raw[i]);

                byte[] screenshot = GetPublicField(states[0], "screenshot") as byte[];
                if (screenshot != null && screenshot.Length > 0)
                    File.WriteAllBytes(Path.Combine(temporary, "screenshot.png"), screenshot);

                for (int i = 0; i < states.Length; i++)
                {
                    string name = new[] { "master", "cpu65816", "spc700", "ppu", "dsp" }[i] + ".json";
                    File.WriteAllText(
                        Path.Combine(temporary, name),
                        JsonValueWriter.Serialize(states[i]) + Environment.NewLine,
                        new UTF8Encoding(false));
                }

                Dictionary<string, object> manifest = new Dictionary<string, object>(StringComparer.Ordinal)
                {
                    { "format", "superzsnes-v0230-portable-state" },
                    { "version", 1 },
                    { "exactness", "complete-source-state" },
                    { "sourceFile", input },
                    { "sourceLength", source.Length },
                    { "sourceSha256", Sha256(source) },
                    { "serializedObjectBytes", objectBytes },
                    { "rawTailBytes", ExpectedRawTailSize },
                    { "assemblyCSharpSha256", Sha256(File.ReadAllBytes(Path.Combine(managed, "Assembly-CSharp.dll"))) },
                    { "stateTypes", ExpectedTypes },
                    { "files", BuildFileManifest(temporary) }
                };
                File.WriteAllText(
                    Path.Combine(temporary, "manifest.json"),
                    JsonValueWriter.Serialize(manifest) + Environment.NewLine,
                    new UTF8Encoding(false));

                if (Directory.Exists(output))
                {
                    if (!options.Overwrite)
                        throw new IOException("Output directory already exists; pass --overwrite to replace it: " + output);
                    Directory.Delete(output, recursive: true);
                }
                Directory.Move(temporary, output);
                Console.WriteLine("Exported " + input);
                Console.WriteLine("  output: " + output);
                Console.WriteLine("  source SHA-256: " + Sha256(source));
                Console.WriteLine("  serialized objects: " + objectBytes + " bytes");
                Console.WriteLine("  raw machine state: " + ExpectedRawTailSize + " bytes");
            }
            finally
            {
                AppDomain.CurrentDomain.AssemblyResolve -= resolver;
                if (Directory.Exists(temporary))
                    Directory.Delete(temporary, recursive: true);
            }
        }

        private static object GetPublicField(object value, string name)
        {
            FieldInfo field = value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.Public);
            return field == null ? null : field.GetValue(value);
        }

        private static byte[] ReadExact(Stream stream, int size)
        {
            byte[] value = new byte[size];
            int offset = 0;
            while (offset < size)
            {
                int count = stream.Read(value, offset, size - offset);
                if (count == 0)
                    throw new EndOfStreamException("State ended while reading raw machine memory");
                offset += count;
            }
            return value;
        }

        private static Dictionary<string, object> BuildFileManifest(string directory)
        {
            Dictionary<string, object> files = new Dictionary<string, object>(StringComparer.Ordinal);
            foreach (string path in Directory.GetFiles(directory).OrderBy(Path.GetFileName, StringComparer.Ordinal))
            {
                byte[] data = File.ReadAllBytes(path);
                files.Add(Path.GetFileName(path), new Dictionary<string, object>(StringComparer.Ordinal)
                {
                    { "length", data.Length },
                    { "sha256", Sha256(data) }
                });
            }
            return files;
        }

        private static string Sha256(byte[] data)
        {
            using (SHA256 sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(data)).Replace("-", string.Empty);
        }

        private sealed class StateTypeBinder : SerializationBinder
        {
            private static readonly HashSet<string> AllowedApplicationTypes = new HashSet<string>(StringComparer.Ordinal)
            {
                "MasterExecutor+SaveStateMaster",
                "CPU65c816+SaveState65816",
                "CPUSPC700+SaveStateSPC700",
                "SNESPPU+PPUParams",
                "DSPAudio+SaveStateData",
                "DSPAudio+SaveStateVoiceData",
                "DSPAudio+EnvMode"
            };

            private readonly Assembly applicationAssembly;

            internal StateTypeBinder(Assembly applicationAssembly)
            {
                this.applicationAssembly = applicationAssembly;
            }

            public override Type BindToType(string assemblyName, string typeName)
            {
                string simple = new AssemblyName(assemblyName).Name;
                if (!string.Equals(simple, "Assembly-CSharp", StringComparison.Ordinal) ||
                    !AllowedApplicationTypes.Contains(typeName))
                    throw new SerializationException("State requested a non-whitelisted type: " + typeName);

                Type type = applicationAssembly.GetType(typeName, throwOnError: true, ignoreCase: false);
                return type;
            }
        }

        private sealed class Options
        {
            internal const string Usage =
                "Usage: SuperZSNESStateExporter.exe --input <state.szstN> --managed-dir <SUPERZSNES_Data\\Managed> --output <directory> [--overwrite]";

            internal string InputPath;
            internal string ManagedDirectory;
            internal string OutputDirectory;
            internal bool Overwrite;
            internal bool ShowHelp;

            internal static Options Parse(string[] args)
            {
                Options options = new Options();
                for (int i = 0; i < args.Length; i++)
                {
                    string arg = args[i];
                    if (arg == "--help" || arg == "-h") options.ShowHelp = true;
                    else if (arg == "--overwrite") options.Overwrite = true;
                    else if (arg == "--input") options.InputPath = Next(args, ref i, arg);
                    else if (arg == "--managed-dir") options.ManagedDirectory = Next(args, ref i, arg);
                    else if (arg == "--output") options.OutputDirectory = Next(args, ref i, arg);
                    else throw new ArgumentException("Unknown argument: " + arg);
                }
                if (!options.ShowHelp &&
                    (string.IsNullOrWhiteSpace(options.InputPath) ||
                     string.IsNullOrWhiteSpace(options.ManagedDirectory) ||
                     string.IsNullOrWhiteSpace(options.OutputDirectory)))
                    throw new ArgumentException(Usage);
                return options;
            }

            private static string Next(string[] args, ref int index, string option)
            {
                if (++index >= args.Length)
                    throw new ArgumentException("Missing value for " + option);
                return args[index];
            }
        }

        private static class JsonValueWriter
        {
            internal static string Serialize(object value)
            {
                StringBuilder output = new StringBuilder(4096);
                Write(output, value, new HashSet<object>(ReferenceEqualityComparer.Instance));
                return output.ToString();
            }

            private static void Write(StringBuilder output, object value, HashSet<object> active)
            {
                if (value == null) { output.Append("null"); return; }
                Type type = value.GetType();
                if (value is string text) { WriteString(output, text); return; }
                if (value is bool boolean) { output.Append(boolean ? "true" : "false"); return; }
                if (value is Enum) { WriteString(output, value.ToString()); return; }
                if (IsNumber(type))
                {
                    output.Append(Convert.ToString(value, CultureInfo.InvariantCulture));
                    return;
                }
                if (value is byte[] bytes && bytes.Length > 4096)
                {
                    Write(output, new Dictionary<string, object>(StringComparer.Ordinal)
                    {
                        { "length", bytes.Length }, { "sha256", Sha256(bytes) }, { "externalized", true }
                    }, active);
                    return;
                }
                if (value is IDictionary dictionary)
                {
                    output.Append('{');
                    bool first = true;
                    List<DictionaryEntry> entries = new List<DictionaryEntry>();
                    IDictionaryEnumerator enumerator = dictionary.GetEnumerator();
                    while (enumerator.MoveNext())
                        entries.Add(new DictionaryEntry(enumerator.Key, enumerator.Value));
                    foreach (DictionaryEntry entry in entries.OrderBy(
                        entry => Convert.ToString(entry.Key, CultureInfo.InvariantCulture), StringComparer.Ordinal))
                    {
                        if (!first) output.Append(',');
                        first = false;
                        WriteString(output, Convert.ToString(entry.Key, CultureInfo.InvariantCulture));
                        output.Append(':');
                        Write(output, entry.Value, active);
                    }
                    output.Append('}');
                    return;
                }
                if (value is IEnumerable enumerable)
                {
                    output.Append('[');
                    bool first = true;
                    foreach (object item in enumerable)
                    {
                        if (!first) output.Append(',');
                        first = false;
                        Write(output, item, active);
                    }
                    output.Append(']');
                    return;
                }

                if (!type.IsValueType && !active.Add(value))
                    throw new InvalidDataException("Cycle found while serializing " + type.FullName);
                output.Append('{');
                bool firstField = true;
                foreach (FieldInfo field in type.GetFields(BindingFlags.Instance | BindingFlags.Public)
                    .OrderBy(field => field.Name, StringComparer.Ordinal))
                {
                    if (!firstField) output.Append(',');
                    firstField = false;
                    WriteString(output, field.Name);
                    output.Append(':');
                    Write(output, field.GetValue(value), active);
                }
                output.Append('}');
                if (!type.IsValueType) active.Remove(value);
            }

            private static bool IsNumber(Type type)
            {
                return type == typeof(byte) || type == typeof(sbyte) || type == typeof(short) ||
                    type == typeof(ushort) || type == typeof(int) || type == typeof(uint) ||
                    type == typeof(long) || type == typeof(ulong) || type == typeof(float) ||
                    type == typeof(double) || type == typeof(decimal);
            }

            private static void WriteString(StringBuilder output, string value)
            {
                output.Append('"');
                foreach (char c in value)
                {
                    switch (c)
                    {
                        case '"': output.Append("\\\""); break;
                        case '\\': output.Append("\\\\"); break;
                        case '\b': output.Append("\\b"); break;
                        case '\f': output.Append("\\f"); break;
                        case '\n': output.Append("\\n"); break;
                        case '\r': output.Append("\\r"); break;
                        case '\t': output.Append("\\t"); break;
                        default:
                            if (c < 32) output.Append("\\u" + ((int)c).ToString("x4"));
                            else output.Append(c);
                            break;
                    }
                }
                output.Append('"');
            }
        }

        private sealed class ReferenceEqualityComparer : IEqualityComparer<object>
        {
            internal static readonly ReferenceEqualityComparer Instance = new ReferenceEqualityComparer();
            public new bool Equals(object x, object y) { return ReferenceEquals(x, y); }
            public int GetHashCode(object obj) { return System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(obj); }
        }
    }
}
