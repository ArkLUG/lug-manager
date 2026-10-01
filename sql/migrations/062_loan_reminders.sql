-- Date the borrower was reminded about an overdue loan ('' = not yet).
ALTER TABLE inventory_loans ADD COLUMN reminded_on TEXT NOT NULL DEFAULT '';
