// Aether Card Manager, GPL-3.0-or-later.
namespace VibeMenuCardManager.Core.Saves;

/// <summary>The import's counts (menu/launcher/pgsbak.h om_pgs_imp; contract/SPEC.md §5.17 Import).</summary>
public sealed class ImportResult
{
    public int Kai, Rings;      // files copied from each
    public int Present;         // records the destination already holds in the same version (left alone)
    public int Skipped;         // folders and files left out
    public int Conflicts;       // records the destination holds in another version: never written over
    public int Older;           // losing copies put in SAVES/_QUARANTINE/IMPORT
    public int Cart;            // saves over 32 KB copied cart-only into <folder>[/0n]/CART
    public int Alternates;      // Rings [SLOT10-19] saves kept as alternates (every slot full)
    public List<string> Destinations { get; } = [];

    /// <summary>IMPORT.TXT's line 1, as om_pgs_import writes it (every skipped save is named on the lines after it).</summary>
    public string ImportTxtLine() => string.Create(System.Globalization.CultureInfo.InvariantCulture,
        $"kai={Kai} rings={Rings} present={Present} skipped={Skipped} conflicts={Conflicts} older={Older} cart_only={Cart} alternates={Alternates}\n");
}

/// <summary>The user cancelled the import between files: every file on the card is whole, IMPORT.TXT is not written,
/// and running the import again finishes the job.</summary>
public sealed class ImportCancelledException(ImportResult soFar) : OperationCanceledException("import cancelled")
{
    public ImportResult SoFar { get; } = soFar;
}

/// <summary>The import stopped part-way (the console shows "IMPORT STOPPED rc=&lt;n&gt;"); running it again is safe.</summary>
public sealed class ImportStoppedException(string message, ImportResult soFar, Exception? inner = null)
    : Exception(message, inner)
{
    public ImportResult SoFar { get; } = soFar;
}
