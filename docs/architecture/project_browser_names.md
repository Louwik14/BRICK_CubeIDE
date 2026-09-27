# Project browser names

Projects remain stored in sixteen stable slots as `0:/BRICK/PROJECT/P00.B6C` through `P15.B6C`. The slot is the identity used by Save, Load, boot restoration, and recovery files. The user name is stored separately in the project document metadata and is read during the boot scan.

Load and Manage list existing projects by their stored names. Save As lists existing named projects for overwrite and one `NEW PROJECT` entry while a free slot remains. Creating a project chooses the first free slot and opens the name editor with `UNTITLED`. Existing numbered files remain readable without conversion; a legacy document without a valid stored name displays `PROJECT NN` until saved with a name. Project lists have no Synth or Drum categories.
